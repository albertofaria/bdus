# ---------------------------------------------------------------------------- #

import re

# ---------------------------------------------------------------------------- #
# project information

project = "BDUS"

with open("../libbdus/include/bdus.h", mode="r", encoding="utf-8") as f:

    contents = f.read()

    [version_major] = re.findall(
        r"(?m)^#define BDUS_HEADER_VERSION_MAJOR (\d+)$", contents
    )

    [version_minor] = re.findall(
        r"(?m)^#define BDUS_HEADER_VERSION_MINOR (\d+)$", contents
    )

    [version_patch] = re.findall(
        r"(?m)^#define BDUS_HEADER_VERSION_PATCH (\d+)$", contents
    )

version = release = f"{version_major}.{version_minor}.{version_patch}"

# ---------------------------------------------------------------------------- #
# general configuration

needs_sphinx = "3.1"

extensions = ["sphinx.ext.extlinks", "breathe", "versionwarning.extension"]

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

primary_domain = "c"
highlight_language = "none"
pygments_style = "sphinx"

rst_prolog = f"""
.. _release-tar: https://github.com/albertofaria/bdus/archive/v{version}.tar.gz

.. |build-badge| image:: https://github.com/albertofaria/bdus/workflows/build/badge.svg?branch=main
    :target: https://github.com/albertofaria/bdus/actions

.. |version-badge| image:: https://img.shields.io/badge/version-{version}-yellow.svg
    :target: https://github.com/albertofaria/bdus/releases

.. |license-badge| image:: https://img.shields.io/badge/license-MIT%20%2F%20GPLv2-blue.svg
    :target: user-manual/licensing.html
"""

linkcheck_ignore = [
    r"https://github\.com/albertofaria/bdus/(archive|blob|tree)/.*"
]

# ---------------------------------------------------------------------------- #
# html output configuration

html_copy_source = False
html_use_index = False
html_scaled_image_link = False
html_show_copyright = False

html_theme = "alabaster"

html_theme_options = {
    "logo": "logo.svg",
    "github_user": "albertofaria",
    "github_repo": "bdus",
    "github_button": True,
    "github_type": "star",
    "github_count": "true",
    "page_width": "1000px",
    "sidebar_width": "220px",
    "show_relbar_bottom": True,
    "fixed_sidebar": True,
}

html_sidebars = {"**": ["about.html", "navigation.html", "searchbox.html"]}

html_static_path = ["static"]
html_css_files = ["custom.css"]

# ---------------------------------------------------------------------------- #
# extension configuration -- sphinx.ext.extlinks

extlinks = {
    "repo-dir": (
        f"https://github.com/albertofaria/bdus/tree/v{version}/%s",
        "",
    ),
    "repo-file": (
        f"https://github.com/albertofaria/bdus/blob/v{version}/%s",
        "",
    ),
    "diff": ("https://github.com/albertofaria/bdus/compare/%s", ""),
}

# ---------------------------------------------------------------------------- #
# extension configuration -- breathe

breathe_default_project = "libbdus"

breathe_projects = {
    "libbdus": "_build/breathe/doxygen/libbdus/xml",
    "kbdus": "_build/breathe/doxygen/kbdus/xml",
}

breathe_projects_source = {
    "libbdus": ("../libbdus/include", ["bdus.h"]),
    "kbdus": ("../kbdus/include", ["kbdus.h"]),
}

breathe_domain_by_extension = {"h": "c"}
breathe_default_members = ("members", "undoc-members")

breathe_doxygen_config_options = {
    "HIDE_SCOPE_NAMES": "YES",  # fixes rendering problems for func. ptr. fields
    "WARN_NO_PARAMDOC": "YES",
}

breathe_show_define_initializer = True
breathe_show_enumvalue_initializer = True

# ---------------------------------------------------------------------------- #
# extension configuration -- versionwarning.extension

versionwarning_messages = {
    "latest": (
        "You are reading the documentation for BDUS' development version."
        ' <a href="/en/stable">Click here for the newest stable release.</a>'
    ),
}

versionwarning_default_message = (
    f"You are reading the documentation for BDUS {version}."
    f" The newest stable release is {{newest}}."
)

versionwarning_body_selector = "body"
versionwarning_banner_html = '<div id="version-warning-banner">{message}</div>'

# ---------------------------------------------------------------------------- #
