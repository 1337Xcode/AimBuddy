package com.aimbuddy.update

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

/** Why an update did not go through. The screens turn each into a sentence. */
enum class UpdateProblem {
    /** GitHub could not be reached, or sent something that is not a list of releases. */
    UNREACHABLE,

    /** The download is not the file GitHub lists, is not a newer AimBuddy, or is not signed at all. */
    NOT_VERIFIED,

    /** The download is an AimBuddy signed with another key than the installed copy, which Android cannot replace with it. */
    DIFFERENT_SIGNER,

    /** The system's installer refused the update. */
    NOT_INSTALLED,
}

/** Where the app's update stands. */
sealed interface UpdateStatus {
    /** Nothing has been checked yet. */
    data object Unknown : UpdateStatus

    data object Checking : UpdateStatus

    data object UpToDate : UpdateStatus

    data class Available(val info: UpdateInfo) : UpdateStatus

    data class Downloading(val info: UpdateInfo, val progress: Float) : UpdateStatus

    /** Downloaded and checked: the screen hands it to the system's installer. */
    data class ReadyToInstall(val info: UpdateInfo, val file: File) : UpdateStatus

    /** With the installer. It may be waiting for the player to confirm. */
    data class Installing(val info: UpdateInfo) : UpdateStatus

    /** Something went wrong; [info] is the update that was being fetched, when there was one, so it can be tried again. */
    data class Failed(val problem: UpdateProblem, val info: UpdateInfo?) : UpdateStatus
}

/**
 * The state of the update, shared between the screens, the downloading code and the receiver that
 * hears back from the system's installer. It lives for as long as the process does.
 */
object UpdateState {
    private val mutable = MutableStateFlow<UpdateStatus>(UpdateStatus.Unknown)
    val status: StateFlow<UpdateStatus> = mutable.asStateFlow()

    fun set(status: UpdateStatus) {
        mutable.value = status
    }
}
