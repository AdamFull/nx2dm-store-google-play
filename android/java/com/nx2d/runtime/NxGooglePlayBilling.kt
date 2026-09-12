package com.nx2d.runtime

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams

// Deliberately NOT referenced anywhere in NxActivity.java (unlike
// NxHaptics) - this object only exists in the compiled app when the
// store_google_play module is enabled (see android/app/build.gradle.kts's
// per-module Kotlin/Java source-dir loop), and com.android.billingclient:
// billing is only a Gradle dependency in that case too. Every function the
// C++ side calls is `@JvmStatic` so it compiles to a genuine static method
// on this class (Kotlin objects otherwise expose instance methods on a
// synthesized singleton, which JNI's GetStaticMethodID cannot see) -
// nothing here needs NxActivity, and nothing in NxActivity needs this, so
// disabling the module removes this file from compilation with no dangling
// reference anywhere.
object NxGooglePlayBilling {
    private val mainHandler = Handler(Looper.getMainLooper())
    private var billingClient: BillingClient? = null

    private val purchasesUpdatedListener =
        PurchasesUpdatedListener { billingResult, purchases ->
            val productIds = mutableListOf<String>()
            val purchaseTokens = mutableListOf<String>()
            purchases?.forEach { purchase ->
                purchase.products.forEach { productId ->
                    productIds.add(productId)
                    purchaseTokens.add(purchase.purchaseToken)
                }
                acknowledgeIfNeeded(purchase)
            }
            nativeOnPurchasesUpdated(
                billingResult.responseCode,
                productIds.toTypedArray(),
                purchaseTokens.toTypedArray(),
            )
        }

    // Google Play Billing has no concept of "own the base game" the way
    // Steam/GOG do - the Play Store already gates who can install/run the
    // APK at all, so there is nothing further to check here. Called from
    // C++ only to kick off the one real async step: connecting the client.
    @JvmStatic
    fun connect(context: Context) {
        val client = billingClient ?: BillingClient.newBuilder(context)
            .setListener(purchasesUpdatedListener)
            .enablePendingPurchases(
                PendingPurchasesParams.newBuilder()
                    .enableOneTimeProducts()
                    .build(),
            )
            .build()
            .also { billingClient = it }
        client.startConnection(object : BillingClientStateListener {
            override fun onBillingSetupFinished(billingResult: BillingResult) {
                nativeOnBillingSetupFinished(billingResult.responseCode)
            }

            override fun onBillingServiceDisconnected() {
                nativeOnBillingServiceDisconnected()
            }
        })
    }

    @JvmStatic
    fun disconnect() {
        billingClient?.endConnection()
    }

    // Every product here is treated as a durable, non-consumable entitlement
    // (store.core's DLC-ownership model) - see acknowledgeIfNeeded(). A game
    // that needs consumable currency-style products isn't served by this
    // backend's generic purchase() call, the same kind of honest scope limit
    // store_gog/store_stove already document for their own SDK gaps.
    @JvmStatic
    fun queryProductDetails(productIds: Array<String>) {
        val client = billingClient ?: return
        val products = productIds.map { productId ->
            QueryProductDetailsParams.Product.newBuilder()
                .setProductId(productId)
                .setProductType(BillingClient.ProductType.INAPP)
                .build()
        }
        val params = QueryProductDetailsParams.newBuilder()
            .setProductList(products)
            .build()
        client.queryProductDetailsAsync(params) { billingResult, result ->
            val ids = mutableListOf<String>()
            val titles = mutableListOf<String>()
            val formattedPrices = mutableListOf<String>()
            for (detail in result.productDetailsList) {
                val offer = detail.oneTimePurchaseOfferDetails
                ids.add(detail.productId)
                titles.add(detail.title)
                formattedPrices.add(offer?.formattedPrice ?: "")
            }
            nativeOnProductDetailsResponse(
                billingResult.responseCode,
                ids.toTypedArray(),
                titles.toTypedArray(),
                formattedPrices.toTypedArray(),
            )
        }
    }

    // Re-fetches ProductDetails immediately before launching - Google's own
    // docs warn against caching it across calls, and the offer token
    // launchBillingFlow needs comes from that same fresh lookup.
    @JvmStatic
    fun purchase(activity: Activity, productId: String) {
        val client = billingClient
        if (client == null) {
            nativeOnPurchaseLaunchFailed(BillingClient.BillingResponseCode.SERVICE_DISCONNECTED)
            return
        }
        mainHandler.post {
            val product = QueryProductDetailsParams.Product.newBuilder()
                .setProductId(productId)
                .setProductType(BillingClient.ProductType.INAPP)
                .build()
            val params = QueryProductDetailsParams.newBuilder()
                .setProductList(listOf(product))
                .build()
            client.queryProductDetailsAsync(params) { billingResult, result ->
                val detail = result.productDetailsList.firstOrNull()
                val offer = detail?.oneTimePurchaseOfferDetails
                val offerToken = offer?.offerToken
                if (detail == null || offerToken == null) {
                    nativeOnPurchaseLaunchFailed(BillingClient.BillingResponseCode.ITEM_UNAVAILABLE)
                    return@queryProductDetailsAsync
                }
                val pdParams = BillingFlowParams.ProductDetailsParams.newBuilder()
                    .setProductDetails(detail)
                    .setOfferToken(offerToken)
                    .build()
                val flowParams = BillingFlowParams.newBuilder()
                    .setProductDetailsParamsList(listOf(pdParams))
                    .build()
                // The actual purchase result arrives later via
                // purchasesUpdatedListener, not here - this return value is
                // only "did the Play-hosted UI open", per Google's own API
                // shape (see the module's platform header comment).
                val launchResult = client.launchBillingFlow(activity, flowParams)
                if (launchResult.responseCode != BillingClient.BillingResponseCode.OK) {
                    nativeOnPurchaseLaunchFailed(launchResult.responseCode)
                }
            }
        }
    }

    // The re-check every game should run at startup and after a purchase
    // completes - Play Billing has no separate "ownership" concept, an
    // unconsumed PURCHASED-state INAPP product IS the entitlement.
    @JvmStatic
    fun queryPurchases() {
        val client = billingClient ?: return
        val params = QueryPurchasesParams.newBuilder()
            .setProductType(BillingClient.ProductType.INAPP)
            .build()
        client.queryPurchasesAsync(params) { billingResult, purchases ->
            val ids = mutableListOf<String>()
            for (purchase in purchases) {
                if (purchase.purchaseState == Purchase.PurchaseState.PURCHASED) {
                    ids.addAll(purchase.products)
                    acknowledgeIfNeeded(purchase)
                }
            }
            nativeOnPurchasesQueried(billingResult.responseCode, ids.toTypedArray())
        }
    }

    // Google auto-refunds any purchase left unacknowledged for 3 days - this
    // is the one step that MUST happen for every durable purchase this
    // backend ever sees, whether from a live purchase or a re-queried one
    // from a previous session.
    private fun acknowledgeIfNeeded(purchase: Purchase) {
        if (purchase.purchaseState != Purchase.PurchaseState.PURCHASED || purchase.isAcknowledged) {
            return
        }
        val params = AcknowledgePurchaseParams.newBuilder()
            .setPurchaseToken(purchase.purchaseToken)
            .build()
        billingClient?.acknowledgePurchase(params) { }
    }

    // Declared external (Kotlin's `native`), implemented in C++
    // (store_google_play_platform.cpp / store_google_play_services.cpp) and
    // resolved by the JVM's own symbol-name convention
    // (Java_com_nx2d_runtime_NxGooglePlayBilling_...) against libnx2d.so,
    // already loaded by the time any of this runs - the first Java-calls-C++
    // direction in this codebase; every existing JNI caller (NxHaptics et
    // al.) only goes the other way. @JvmStatic here is what makes these
    // compile to real static methods rather than instance methods on
    // Kotlin's synthesized object singleton.
    @JvmStatic
    private external fun nativeOnBillingSetupFinished(responseCode: Int)

    @JvmStatic
    private external fun nativeOnBillingServiceDisconnected()

    @JvmStatic
    private external fun nativeOnProductDetailsResponse(
        responseCode: Int,
        productIds: Array<String>,
        titles: Array<String>,
        formattedPrices: Array<String>,
    )

    @JvmStatic
    private external fun nativeOnPurchasesUpdated(
        responseCode: Int,
        productIds: Array<String>,
        purchaseTokens: Array<String>,
    )

    @JvmStatic
    private external fun nativeOnPurchaseLaunchFailed(responseCode: Int)

    @JvmStatic
    private external fun nativeOnPurchasesQueried(responseCode: Int, productIds: Array<String>)
}
