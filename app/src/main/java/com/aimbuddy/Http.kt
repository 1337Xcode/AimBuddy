package com.aimbuddy

import java.io.FilterInputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/** How the app reaches GitHub: plain GET requests with timeouts, and an error for any answer that is not a success. */
object Http {
    private const val CONNECT_TIMEOUT_MS = 10_000
    private const val READ_TIMEOUT_MS = 20_000
    private const val USER_AGENT = "AimBuddy"

    /**
     * Opens a GET request and returns it once the answer is a success; the caller reads the stream
     * and disconnects. Only HTTPS is accepted, unless [allowInsecure] is set (debug builds, which
     * test against a server on the developer's computer).
     */
    fun open(url: String, accept: String? = null, allowInsecure: Boolean = false): HttpURLConnection {
        require(url.startsWith("https://") || (allowInsecure && url.startsWith("http://"))) { "Only HTTPS addresses are used: $url" }
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = CONNECT_TIMEOUT_MS
        connection.readTimeout = READ_TIMEOUT_MS
        connection.setRequestProperty("User-Agent", USER_AGENT)
        accept?.let { connection.setRequestProperty("Accept", it) }
        val code = connection.responseCode
        if (code !in 200..299) {
            connection.disconnect()
            throw IOException("HTTP $code for $url")
        }
        return connection
    }

    /** Opens a GET request and returns what it answers as a stream. Closing the stream also ends the connection. */
    fun openStream(url: String, allowInsecure: Boolean = false): InputStream {
        val connection = open(url, allowInsecure = allowInsecure)
        return object : FilterInputStream(connection.inputStream) {
            override fun close() {
                try {
                    super.close()
                } finally {
                    connection.disconnect()
                }
            }
        }
    }

    /** Reads a small text answer. */
    fun getText(url: String, accept: String? = null, allowInsecure: Boolean = false): String {
        val connection = open(url, accept, allowInsecure)
        try {
            return connection.inputStream.bufferedReader().use { it.readText() }
        } finally {
            connection.disconnect()
        }
    }
}
