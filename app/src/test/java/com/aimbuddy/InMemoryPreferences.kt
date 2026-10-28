package com.aimbuddy

import android.content.SharedPreferences
import java.util.concurrent.ConcurrentHashMap

/** Preferences kept in memory, for tests of code that reads and writes them. Like the real ones, it can be used from several threads. */
class InMemoryPreferences : SharedPreferences {
    private val values = ConcurrentHashMap<String, Any>()

    override fun getAll(): MutableMap<String, *> = values.toMutableMap()
    override fun getString(key: String, defValue: String?): String? = values[key] as? String ?: defValue
    override fun getStringSet(key: String, defValues: MutableSet<String>?): MutableSet<String>? = defValues
    override fun getInt(key: String, defValue: Int): Int = values[key] as? Int ?: defValue
    override fun getLong(key: String, defValue: Long): Long = values[key] as? Long ?: defValue
    override fun getFloat(key: String, defValue: Float): Float = values[key] as? Float ?: defValue
    override fun getBoolean(key: String, defValue: Boolean): Boolean = values[key] as? Boolean ?: defValue
    override fun contains(key: String): Boolean = values.containsKey(key)
    override fun registerOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) = Unit
    override fun unregisterOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) = Unit

    override fun edit(): SharedPreferences.Editor = object : SharedPreferences.Editor {
        private val changes = mutableMapOf<String, Any?>()

        override fun putString(key: String, value: String?) = apply { changes[key] = value }
        override fun putStringSet(key: String, values: MutableSet<String>?) = apply { changes[key] = values }
        override fun putInt(key: String, value: Int) = apply { changes[key] = value }
        override fun putLong(key: String, value: Long) = apply { changes[key] = value }
        override fun putFloat(key: String, value: Float) = apply { changes[key] = value }
        override fun putBoolean(key: String, value: Boolean) = apply { changes[key] = value }
        override fun remove(key: String) = apply { changes[key] = null }
        override fun clear() = apply { values.clear() }

        override fun commit(): Boolean {
            apply()
            return true
        }

        override fun apply() {
            changes.forEach { (key, value) -> if (value == null) values.remove(key) else values[key] = value }
        }
    }
}
