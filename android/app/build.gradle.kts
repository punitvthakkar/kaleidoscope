// The app: Kotlin shell plus the native library and assets android/build.sh made in
// <fx.out>. The one dependency beyond the Android Gradle plugin and its built-in Kotlin is
// MediaPipe's hand tracker, pinned to the newest release before 1.0 (as the web pins it).
plugins { id("com.android.application") }

val fxOut = file(providers.gradleProperty("fx.out").get())
layout.buildDirectory.set(File(fxOut, "gradle"))

android {
    namespace = "app.fractonaut"
    compileSdk = 36
    defaultConfig {
        applicationId = "app.fractonaut"
        // build.sh --out dev: "fractonaut dev", installed beside the released app.
        val dev = providers.gradleProperty("fx.dev").orNull == "1"
        if (dev) applicationIdSuffix = ".dev"
        manifestPlaceholders["appLabel"] = if (dev) "fractonaut dev" else "fractonaut"
        minSdk = 31
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
    }
    sourceSets["main"].apply {
        jniLibs.srcDir(File(fxOut, "jniLibs"))
        assets.srcDir(File(fxOut, "assets"))
    }
    buildTypes {
        getByName("debug") { isMinifyEnabled = false }
        // build.sh --release: for performance testing. Signed with the local debug key so
        // it installs like the debug build.
        getByName("release") {
            isMinifyEnabled = true
            isDebuggable = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    packaging { jniLibs { useLegacyPackaging = false } }
    androidResources { noCompress += listOf("html", "png", "glsl", "txt", "task") }
}

dependencies {
    implementation("com.google.mediapipe:tasks-vision:0.10.35")
}
