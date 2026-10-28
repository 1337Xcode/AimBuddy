package com.aimbuddy

import java.io.IOException

/** How far a download has got: what has arrived, what there will be in all, and how fast it is coming. */
data class TransferProgress(val bytesDone: Long = 0, val bytesTotal: Long = 0, val bytesPerSecond: Long = 0) {

    /** 0 to 1. */
    val fraction: Float
        get() = if (bytesTotal > 0) (bytesDone.toDouble() / bytesTotal).toFloat().coerceIn(0f, 1f) else 0f

    /** Whole seconds left at the present speed, or null while the speed is not known yet or nothing is left. */
    val secondsLeft: Long?
        get() {
            val remaining = bytesTotal - bytesDone
            if (bytesPerSecond <= 0 || remaining <= 0) {
                return null
            }
            return (remaining + bytesPerSecond - 1) / bytesPerSecond
        }
}

/**
 * Measures how fast bytes arrive, over the last few seconds and not over the whole download, so that the
 * speed follows a connection that gets better or worse. Give it the time and the bytes that have arrived
 * in all; it says how many bytes a second that makes.
 */
class TransferMeter(private val windowMs: Long = WINDOW_MS) {
    private val samples = ArrayDeque<Pair<Long, Long>>()

    /** Notes that [bytesDone] bytes had arrived at [nowMs] and returns the speed in bytes a second, 0 until there is enough to go on. */
    fun add(nowMs: Long, bytesDone: Long): Long {
        samples.addLast(nowMs to bytesDone)
        while (samples.size > 2 && nowMs - samples.first().first > windowMs) {
            samples.removeFirst()
        }
        val (firstMs, firstBytes) = samples.first()
        val spanMs = nowMs - firstMs
        if (spanMs < LEAST_SPAN_MS || bytesDone < firstBytes) {
            return 0
        }
        return (bytesDone - firstBytes) * 1000 / spanMs
    }

    private companion object {
        const val WINDOW_MS = 4_000L
        const val LEAST_SPAN_MS = 600L
    }
}

/** The player asked for a download to stop. Not a failure: nothing is said about it. */
class DownloadCancelledException : IOException("The download was cancelled")

/** Why a download did not finish, in the terms a player can do something about. */
enum class TransferFailure {
    /** The phone could not reach the store, or the connection broke on the way. */
    OFFLINE,

    /** What arrived is not what the store listed: the wrong size or the wrong checksum. */
    DAMAGED,

    /** The phone ran out of room for the files. */
    NO_SPACE,

    /** Anything else, such as the store answering with an error. */
    OTHER;

    companion object {
        /** Tells from the error, and the errors it was caused by, what went wrong. */
        fun of(error: Throwable): TransferFailure {
            var current: Throwable? = error
            var depth = 0
            while (current != null && depth < MAX_CAUSES) {
                val message = current.message.orEmpty()
                when {
                    current is java.net.UnknownHostException || current is java.net.ConnectException ||
                        current is java.net.SocketException || current is java.net.SocketTimeoutException -> return OFFLINE
                    message.contains("ENOSPC") || message.contains("No space left", ignoreCase = true) -> return NO_SPACE
                    message.contains("checksum", ignoreCase = true) || message.contains("larger than the store says") -> return DAMAGED
                }
                current = current.cause
                depth++
            }
            return OTHER
        }

        private const val MAX_CAUSES = 8
    }
}
