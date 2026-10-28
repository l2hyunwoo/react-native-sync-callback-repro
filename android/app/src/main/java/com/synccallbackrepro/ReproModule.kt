package com.synccallbackrepro

import android.content.Context
import android.util.Log
import com.facebook.react.ReactApplication
import com.facebook.react.bridge.Callback
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Holds JS callbacks without ever invoking them, then drops them and reloads.
 * The dropped callbacks are destroyed by the fbjni HybridData destructor thread
 * while the old runtime is being torn down.
 */
class ReproModule(context: ReactApplicationContext) : ReactContextBaseJavaModule(context) {
  override fun getName(): String = "Repro"

  @ReactMethod
  fun hold(callback: Callback) {
    synchronized(held) { held.add(callback) }
  }

  @ReactMethod(isBlockingSynchronousMethod = true)
  fun nextCycle(): Double {
    val prefs = reactApplicationContext.getSharedPreferences("repro", Context.MODE_PRIVATE)
    val cycle = prefs.getInt("cycle", 0) + 1
    prefs.edit().putInt("cycle", cycle).commit()
    return cycle.toDouble()
  }

  @ReactMethod
  fun dropAndReload() {
    val app = reactApplicationContext.applicationContext as ReactApplication
    Thread {
      // Keep every core busy so the destructor thread gets preempted, like on a loaded device.
      val stop = AtomicBoolean(false)
      repeat(Runtime.getRuntime().availableProcessors() * 2) {
        Thread { while (!stop.get()) {} }.start()
      }
      val dropped = synchronized(held) { held.size.also { held.clear() } }
      Log.w("Repro", "dropping $dropped callbacks and reloading")
      Runtime.getRuntime().gc()
      app.reactHost?.reload("repro")
      val end = System.currentTimeMillis() + 1500
      while (System.currentTimeMillis() < end) {
        Runtime.getRuntime().gc()
        Thread.sleep(20)
      }
      stop.set(true)
    }.start()
  }

  companion object {
    private val held = mutableListOf<Callback>()
  }
}
