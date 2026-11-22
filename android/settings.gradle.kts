// Packaging only: android/build.sh compiles the native library and the assets, then
// runs `gradle -Pfx.out=<dir> :app:assembleDebug`. Every version is pinned here.
pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
    plugins { id("com.android.application") version "9.5.1" }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories { google(); mavenCentral() }
}
rootProject.name = "fractonaut"
include(":app")
