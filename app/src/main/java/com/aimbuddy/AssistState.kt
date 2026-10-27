package com.aimbuddy

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * What the assist is doing, shared between the service that runs it and the screen that shows it.
 *
 * The service is the only writer. The screen reads it as a flow, so it stays right when the app is
 * closed and opened again while the service keeps running.
 */
object AssistState {

    enum class Phase { IDLE, STARTING, RUNNING, STOPPING }

    /** Why a run ended when it was not the player's choice. The screen turns each into a sentence. */
    enum class Failure { CAPTURE_DENIED, OVERLAY_DENIED, MODEL_FAILED, START_FAILED, CAPTURE_ENDED }

    /** How the touch helper, which adds the assist's finger to the touchscreen, is doing. */
    sealed interface Touch {
        /** Not started, or the assist has not been asked for. */
        data object Off : Touch

        data object Starting : Touch

        data class Connected(val route: TouchHelper.Route) : Touch

        /** The helper could not be used; the assist still draws targets but cannot move the aim. */
        data class Unavailable(val reason: Int, val detail: String) : Touch
    }

    /** One reading of the state. */
    data class Snapshot(
        val phase: Phase = Phase.IDLE,
        val failure: Failure? = null,
        val touch: Touch = Touch.Off,
    ) {
        val isActive: Boolean get() = phase == Phase.STARTING || phase == Phase.RUNNING
    }

    private val mutable = MutableStateFlow(Snapshot())
    val state: StateFlow<Snapshot> = mutable.asStateFlow()

    fun set(phase: Phase, failure: Failure? = null) = mutable.update { it.copy(phase = phase, failure = failure) }

    fun setTouch(touch: Touch) = mutable.update { it.copy(touch = touch) }
}
