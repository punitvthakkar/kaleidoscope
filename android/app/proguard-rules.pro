# Native code finds these by name (RegisterNatives, GetStaticMethodID in
# android/native/host.cpp), and the page calls the bridge through addJavascriptInterface.
-keep class app.fractonaut.Host { *; }
-keepclassmembers class * { @android.webkit.JavascriptInterface <methods>; }
