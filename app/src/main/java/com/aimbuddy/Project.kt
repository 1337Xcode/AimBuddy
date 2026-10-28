package com.aimbuddy

/** Where the project lives on GitHub: the page the app links to, and the place it updates itself and its models from. */
object Project {
    const val OWNER = "1337Xcode"
    const val REPOSITORY = "AimBuddy"
    const val BRANCH = "master"

    const val URL = "https://github.com/$OWNER/$REPOSITORY"
    const val LICENSE_URL = "$URL/blob/$BRANCH/LICENSE"

    /** The project's published releases, which the app reads to see whether there is a newer version. */
    const val RELEASES_URL = "https://api.github.com/repos/$OWNER/$REPOSITORY/releases?per_page=10"

    /** The page of those releases, where a person downloads one by hand. */
    const val RELEASES_PAGE_URL = "$URL/releases"

    /** Where the model store's files are served from: the project's `model` folder. */
    const val STORE_URL = "https://raw.githubusercontent.com/$OWNER/$REPOSITORY/$BRANCH/model"
}
