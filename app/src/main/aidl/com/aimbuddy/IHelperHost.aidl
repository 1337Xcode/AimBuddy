package com.aimbuddy;

/**
 * A small service that Shizuku runs for the app with the privileges of the shell user. Its only job
 * is to start and watch the touch helper program, which needs those privileges to read the
 * touchscreen and create a virtual one.
 */
interface IHelperHost {
    /** Shizuku calls this when the service is being removed. The transaction code is fixed by Shizuku. */
    void destroy() = 16777114;

    /**
     * Starts the touch helper and waits for it to say which port it listens on.
     * `command` is the program's path followed by its arguments. Returns the port, or -1 when the
     * helper did not start within `timeoutMs` (see log() for why).
     */
    int start(in String[] command, int timeoutMs) = 1;

    /** Stops the touch helper if it is running. */
    void stop() = 2;

    /** The last lines the touch helper printed, for finding out why it stopped. */
    String log() = 3;

    boolean isRunning() = 4;
}
