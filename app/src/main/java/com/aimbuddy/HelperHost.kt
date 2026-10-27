package com.aimbuddy

import kotlin.system.exitProcess

/**
 * The service Shizuku runs, as the shell user, to start the touch helper program.
 *
 * It does nothing but launch the program and watch it, so the app can learn the port the helper
 * listens on and tell the player why it stopped. Shizuku creates it by class name in its own
 * process, so it needs a public constructor without arguments and must not be removed by shrinking.
 */
class HelperHost : IHelperHost.Stub() {

    private val lock = Any()
    private var helper: HelperProcess? = null
    private var startError = ""

    override fun destroy() {
        stop()
        exitProcess(0)
    }

    override fun start(command: Array<String>, timeoutMs: Int): Int {
        stop()
        val started = try {
            HelperProcess(ProcessBuilder(command.toList()).redirectErrorStream(true).start())
        } catch (e: Exception) {
            synchronized(lock) { startError = e.message ?: e.javaClass.simpleName }
            return HelperProcess.NOT_LISTENING
        }
        synchronized(lock) {
            helper = started
            startError = ""
        }
        return started.awaitPort(timeoutMs.toLong())
    }

    override fun stop() {
        val running = synchronized(lock) {
            val current = helper
            helper = null
            current
        }
        running?.stop()
    }

    override fun log(): String = synchronized(lock) { helper }?.log() ?: synchronized(lock) { startError }

    override fun isRunning(): Boolean = synchronized(lock) { helper }?.isAlive() == true
}
