package com.aimbuddy.update

/**
 * A version number as the project writes it (`0.3.0-beta.3`): the numbers of the release, then an
 * optional pre-release tag. Versions order the way Semantic Versioning says: a release is newer than
 * any pre-release of the same numbers, and pre-release tags are compared part by part, numbers as
 * numbers.
 */
class AppVersion private constructor(
    private val numbers: List<Int>,
    private val preRelease: List<String>,
) : Comparable<AppVersion> {

    /** True for a version with a tag such as `beta.3`. */
    val isPreRelease: Boolean get() = preRelease.isNotEmpty()

    override fun compareTo(other: AppVersion): Int {
        val length = maxOf(numbers.size, other.numbers.size)
        for (index in 0 until length) {
            val difference = numbers.getOrElse(index) { 0 }.compareTo(other.numbers.getOrElse(index) { 0 })
            if (difference != 0) {
                return difference
            }
        }
        // The same numbers: a release comes after its pre-releases.
        if (preRelease.isEmpty() || other.preRelease.isEmpty()) {
            return other.preRelease.size.compareTo(preRelease.size)
        }
        for (index in 0 until minOf(preRelease.size, other.preRelease.size)) {
            val difference = comparePart(preRelease[index], other.preRelease[index])
            if (difference != 0) {
                return difference
            }
        }
        return preRelease.size.compareTo(other.preRelease.size)
    }

    override fun equals(other: Any?): Boolean = other is AppVersion && compareTo(other) == 0

    override fun hashCode(): Int = numbers.dropLastWhile { it == 0 }.hashCode() * 31 + preRelease.hashCode()

    override fun toString(): String =
        numbers.joinToString(".") + if (preRelease.isEmpty()) "" else "-" + preRelease.joinToString(".")

    companion object {
        /** `v` is optional; a build tag after `+` is ignored, as Semantic Versioning says. */
        private val PATTERN = Regex("""v?(\d+(?:\.\d+)*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z.-]+)?""")

        /** Reads `0.3.0`, `v0.3.0-beta.3` and the like. Returns null when the text is not a version. */
        fun parse(text: String): AppVersion? {
            val match = PATTERN.matchEntire(text.trim()) ?: return null
            val numbers = match.groupValues[1].split('.').map { it.toIntOrNull() ?: return null }
            val tag = match.groupValues[2]
            return AppVersion(numbers, if (tag.isEmpty()) emptyList() else tag.split('.'))
        }

        /** Two pre-release parts: numbers are compared as numbers and come before words. */
        private fun comparePart(a: String, b: String): Int {
            val aNumber = a.toIntOrNull()
            val bNumber = b.toIntOrNull()
            return when {
                aNumber != null && bNumber != null -> aNumber.compareTo(bNumber)
                aNumber != null -> -1
                bNumber != null -> 1
                else -> a.compareTo(b)
            }
        }
    }
}
