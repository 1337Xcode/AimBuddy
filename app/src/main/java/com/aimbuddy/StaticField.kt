package com.aimbuddy

import java.util.Random

/**
 * The grey levels, from 0 (black) to 255 (white), of a field of TV static with [count] grains.
 *
 * Every grain is independent of the others and any level is as likely as any other, so no part of
 * the field is darker, lighter or more regular than another, and nothing in it has a shape. The
 * launcher icon is made the same way at build time (see app/static-icon.gradle).
 */
internal fun staticLevels(random: Random, count: Int): IntArray = IntArray(count) { random.nextInt(LEVELS) }

/** Levels in a grain: every grey a byte can hold. */
private const val LEVELS = 256
