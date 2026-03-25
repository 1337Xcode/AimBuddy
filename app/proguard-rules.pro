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
# Jetpack Compose — R8 rules are bundled inside the Compose libraries, but
# we add a few extra guards for stability
# ---------------------------------------------------------------------------
-dontwarn androidx.compose.**
-keep class androidx.compose.** { *; }

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
# AndroidSVG
# ---------------------------------------------------------------------------
-keep class com.caverock.androidsvg.** { *; }

# ---------------------------------------------------------------------------
# Generic: keep line-number info in stack traces (useful for crash reports)
# ---------------------------------------------------------------------------
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
