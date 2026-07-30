"""Sphinx configuration for the sioxx documentation."""

from pathlib import Path
import json
import re


repository_root = Path(__file__).resolve().parents[1]
cmake_project = (repository_root / "CMakeLists.txt").read_text(encoding="utf-8")
version_match = re.search(
    r"project\s*\(\s*sioxx.*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
    cmake_project,
    re.DOTALL,
)
if version_match is None:
    raise RuntimeError("Could not read the sioxx version from CMakeLists.txt")

project = "sioxx"
copyright = "2026, jfayot"
author = "jfayot"
release = version_match.group(1)
version = release

extensions = ["breathe", "sphinx.ext.graphviz"]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

html_theme = "pydata_sphinx_theme"
html_title = f"sioxx {release}"
html_static_path = ["_static"]
html_css_files = ["sioxx.css"]

documentation_versions = json.loads(
    (Path(__file__).parent / "_static" / "versions.json").read_text(
        encoding="utf-8"
    )
)

html_theme_options = {
    "navbar_align": "left",
    "navbar_start": ["navbar-logo"],
    "navbar_center": ["navbar-nav"],
    "navbar_end": ["version-selector", "github-stats", "theme-switcher"],
    "navbar_persistent": ["search-field"],
    "header_links_before_dropdown": 3,
    "search_bar_text": "Search",
    "show_prev_next": True,
    "back_to_top_button": True,
    "logo": {
        "text": "sioxx",
        "image_light": "_static/sioxx-logo.svg",
        "image_dark": "_static/sioxx-logo.svg",
    },
    "icon_links": [
        {
            "name": "GitHub",
            "url": "https://github.com/jfayot/sioxx",
            "icon": "fa-brands fa-github",
            "type": "fontawesome",
        },
    ],
}

html_context = {
    "documentation_versions": documentation_versions,
    "current_documentation_version": release,
    "github_user": "jfayot",
    "github_repo": "sioxx",
    "github_version": "main",
    "doc_path": "docs",
}

# The three top-level pages are always available in the header navigation, so
# a primary sidebar would be redundant (and empty on most pages).
html_sidebars = {"**": []}

breathe_default_project = "sioxx"
primary_domain = "cpp"
highlight_language = "cpp"
graphviz_output_format = "svg"
