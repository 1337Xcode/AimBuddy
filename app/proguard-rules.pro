# =============================================================================
# ProGuard / R8 rules for AimBuddy
# =============================================================================

# ---------------------------------------------------------------------------
# JNI — keep all native method entry points so R8 doesn't rename or remove them
# ---------------------------------------------------------------------------
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the JNI bridge class explicitly (adjust package/class name if needed)
-keep class com.aimbuddy.** { *; }

# ---------------------------------------------------------------------------
# Android components — must never be renamed
# ---------------------------------------------------------------------------
-keep public class * extends android.app.Activity
-keep public class * extends android.app.Service
-keep public class * extends android.content.BroadcastReceiver
-keep public class * extends android.content.ContentProvider

# ---------------------------------------------------------------------------
# Kotlin — preserve metadata annotations for reflection
# ---------------------------------------------------------------------------
-keep class kotlin.Metadata { *; }
-dontwarn kotlin.**
-dontnote kotlin.**

# ---------------------------------------------------------------------------
# Jetpack Compose
#
# Do NOT add a blanket `-keep class androidx.compose.** { *; }` here. Compose
# ships its own consumer R8 rules, so a catch-all keep adds nothing and instead
# disables shrinking across every Compose artifact. With material-icons-extended
# on the classpath that pinned thousands of unused icon accessors into the dex
# and cost roughly 20 MB of APK. The app only references nine icons; R8 finds
# them through the `com.aimbuddy.**` keep above and drops the rest.
# ---------------------------------------------------------------------------
-dontwarn androidx.compose.**

# ---------------------------------------------------------------------------
# Aggressive R8 optimisations
# ---------------------------------------------------------------------------
# Allow R8 to widen member visibility for better inlining opportunities
-allowaccessmodification

# Remove Android logging in release builds (saves size and avoids info leaks)
-assumenosideeffects class android.util.Log {
    public static int v(...);
    public static int d(...);
    public static int i(...);
    public static int w(...);
    public static int e(...);
}

# ---------------------------------------------------------------------------
# Generic: keep line-number info in stack traces (useful for crash reports)
# ---------------------------------------------------------------------------
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
