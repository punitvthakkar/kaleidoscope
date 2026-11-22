plugins { id("com.android.application") apply false }

// Each build.sh output directory gets its own Gradle build directories.
val fxOut = providers.gradleProperty("fx.out").orNull
    ?: error("run android/build.sh (it passes -Pfx.out=<dir>)")
layout.buildDirectory.set(file("$fxOut/gradle-root"))
