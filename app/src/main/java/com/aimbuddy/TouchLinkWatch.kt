package com.aimbuddy

/**
 * What the native side says about the connection to the touch helper, as `NativeBridge.touchStatus()` packs
 * it: whether it is connected (bit 24), why it stopped if it did (bits 8 to 23) and where the helper
 * stands (the low byte, a `touch::protocol::State`).
 */
internal data class LinkStatus(val connected: Boolean, val state: Int, val reason: Int)

internal fun decodeLinkStatus(raw: Int): LinkStatus =
    LinkStatus(connected = (raw ushr 24) and 1 == 1, state = raw and 0xFF, reason = (raw ushr 8) and 0xFFFF)

/**
 * Whether a helper that dropped out is worth starting again. The player holding both volume keys is taking
 * the screen back on purpose, and starting the helper again would take it from them once more.
 */
internal fun isWorthRestarting(reason: Int): Boolean = reason != TouchHelper.Reason.ESCAPE_KEYS

/**
 * Lets a helper be started again a few times, not for ever: a helper that keeps dropping out has a
 * cause that another start will not cure, and the player should be told. A restart counts for [windowMs].
 */
internal class RestartBudget(private val maxRestarts: Int, private val windowMs: Long) {
    private val restartTimes = ArrayDeque<Long>()

    /** True, and counts one, when a restart is allowed at [nowMs]. */
    fun tryUse(nowMs: Long): Boolean {
        while (restartTimes.isNotEmpty() && nowMs - restartTimes.first() > windowMs) {
            restartTimes.removeFirst()
        }
        if (restartTimes.size >= maxRestarts) {
            return false
        }
        restartTimes.addLast(nowMs)
        return true
    }
}
