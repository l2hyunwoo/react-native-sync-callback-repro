# SyncCallback teardown crash reproducer

React Native 0.84.0, Android, New Architecture (bridgeless), Hermes.

`~SyncCallback` calls `wrapper_.lock()` on the thread that destroys it. On Android,
callbacks passed to native modules are destroyed on the fbjni `HybridData Dest` thread.
When this happens while the old runtime is torn down, the local `shared_ptr` becomes the
last strong reference, and `~CallbackWrapper` destroys its `jsi::Function` on that thread
after the runtime memory is freed. The result is a SIGSEGV.

## What the app does

Each cycle:

1. `App.tsx` passes 20,000 callbacks to `Repro.hold()`. Native code keeps them and never calls them.
2. After 500 ms, `Repro.dropAndReload()` clears the list, runs GC, and calls
   `reactHost.reload()`. It keeps all cores busy for 1.5 s so the destructor thread gets
   preempted.
3. The new runtime starts the next cycle. The cycle number is shown on screen.

## Run (stock React Native)

```sh
npm install
cd android && ./gradlew :app:assembleRelease -PreactNativeArchitectures=arm64-v8a
adb install -r app/build/outputs/apk/release/app-release.apk
adb shell pm clear com.synccallbackrepro   # resets the cycle number
adb shell am start -n com.synccallbackrepro/.MainActivity
```

Let it run for up to 150 s, then read the crash:

```sh
adb logcat -b crash -d
```

Expected: `signal 11 (SIGSEGV)` on the thread `HybridData Dest`.

## Symbolized stack

The `libreactnative.so` in the APK is stripped, but the prefab copy in the same AAR is not
and has the same Build ID. Extract it and pass it to `ndk-stack`:

```sh
mkdir -p /tmp/rn-syms && unzip -o -j \
  ~/.gradle/caches/modules-2/files-2.1/com.facebook.react/react-android/0.84.0/*/react-android-0.84.0-release.aar \
  'prefab/modules/reactnative/libs/android.arm64-v8a/libreactnative.so' -d /tmp/rn-syms
adb logcat -b crash -d > crash.txt
$ANDROID_HOME/ndk/<version>/ndk-stack -sym /tmp/rn-syms -i crash.txt
```

The stack is:

```
facebook::jsi::Pointer::~Pointer()
facebook::react::CallbackWrapper::~CallbackWrapper()
std::shared_ptr<CallbackWrapper>::~shared_ptr()
facebook::react::SyncCallback<void ()>::~SyncCallback()
std::shared_ptr<SyncCallback<void ()>>::~shared_ptr()
facebook::react::AsyncCallback<>::~AsyncCallback()
createJavaCallback(...)::$_0::~$_0()
facebook::react::JCxxCallbackImpl::~JCxxCallbackImpl()
libfbjni.so
com.facebook.jni.HybridData$Destructor.destruct
com.facebook.jni.DestructorThread$1.run
```

## Test the fix

`patches/sync-callback-destructor.patch` changes `~SyncCallback` so that it does not take a
strong reference. It posts a task with only the `weak_ptr` to the `CallInvoker`, and the
JS thread destroys the wrapper.

To test it, apply the patch and build React Native from source.
`android/settings.gradle` enables `includeBuild('../node_modules/react-native')` when you
pass `-PreactNativeFromSource=true`:

```sh
(cd node_modules/react-native && patch -p1 < ../../patches/sync-callback-destructor.patch)
cd android
ANDROID_HOME=~/Library/Android/sdk ./gradlew :app:assembleRelease \
  -PreactNativeArchitectures=arm64-v8a -PreactNativeFromSource=true
```

To go back to stock, run `npm ci`.

## C++ stress program

`cpp/repro.cpp` is a host-side stress program for the same race: it destroys
`SyncCallback`s on background threads while the runtime is torn down. The results above
come from the Android app only.
