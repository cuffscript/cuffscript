import os
import re
import codecs
import queue
import shutil
import tempfile
import threading
import time
import traceback
import webbrowser
import subprocess
import signal
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk
from tkinter import font as tkfont

MINIMUM_PYTHON_VERSION = (3, 8)


class Document:
    def __init__(self):
        self.frame = None
        self.editor = None
        self.line_numbers = None
        self.vertical_scrollbar = None
        self.horizontal_scrollbar = None
        self.path = None
        self.dirty = False
        self.line_ending = "\n"
        self.has_bom = False
        self.gutter_anchor = None
        self.disk_stamp = None
        self.numbered_lines = 0
        self.untitled_number = 0
        self.tab = None
        self.tab_accent = None
        self.tab_title = None
        self.tab_close = None


def _active(name):
    def getter(self):
        return getattr(self.doc, name)

    def setter(self, value):
        setattr(self.doc, name, value)

    return property(getter, setter)


class CuffScriptIDE:
    KEYWORDS = {
        "constant",
        "global",
        "add",
        "remove",
        "if",
        "else",
        "loop",
        "repeat",
        "while",
        "each",
        "stop",
        "skip",
        "return",
        "fail",
        "await",
        "use",
        "from",
        "in",
        "to",
        "at",
        "do",
        "end",
        "note",
        "endnote",
        "func",
        "class",
        "extends",
        "with",
        "async",
        "pure",
        "private",
        "or_else",
        "self",
        "super",
        "is",
        "matches",
        "isa",
        "not",
        "and",
        "or",
        "true",
        "false",
        "empty",
    }
    TYPE_NAMES = {
        "any",
        "number",
        "str",
        "boolean",
        "list",
        "map",
        "match",
        "pattern",
        "object",
        "empty",
    }
    # Kept equal to the engine's by `python3 tools/docs.py check`.
    BUILTINS = {
        "contains",
        "copy",
        "count",
        "find",
        "find_all",
        "index_of",
        "input",
        "length",
        "match",
        "pattern",
        "print",
        "replace",
        "replace_all",
        "split",
        "task_done",
        "to_boolean",
        "to_number",
        "to_str",
        "type_of",
    }
    LIBRARY_FUNCTIONS = set(
        (
            "csv_parse csv_parse_records csv_write csv_write_records datetime_format datetime_make datetime_offset "
            "datetime_parse datetime_parts file_add file_copy file_exist file_isdir file_list file_makedir file_move "
            "file_read file_readlines file_remove file_removedir file_size file_write from_json hash_base64_decode "
            "hash_base64_encode hash_base64url_decode hash_base64url_encode hash_crc32 hash_equal hash_from_bytes "
            "hash_hex_decode hash_hex_encode hash_hmac_sha256 hash_hmac_sha512 hash_md5 hash_pbkdf2_sha256 hash_sha1 "
            "hash_sha256 hash_sha512 hash_to_bytes hw_analog hw_battery hw_beep hw_button hw_led hw_pin hw_pwm hw_screen_text "
            "hw_sensor hw_serial hw_supports list_average list_flatten list_join list_range list_reverse list_sort list_sum "
            "list_unique map_entries map_has_key map_keys map_merge map_values math_abs math_acos math_asin math_atan "
            "math_atan2 math_bit_and math_bit_not math_bit_or math_bit_shl math_bit_shr math_bit_test math_bit_xor "
            "math_ceil math_clamp math_cos math_e math_exp math_floor math_from_base math_log math_log10 math_log2 math_max "
            "math_min math_mod math_pi math_pow math_round math_sign math_sin math_sqrt math_tan math_to_base math_trunc network_get "
            "network_post path_basename path_dirname path_extension path_is_absolute path_join path_normalize path_relative "
            "path_split path_stem path_with_extension random_choice random_float random_int random_seed random_shuffle "
            "str_char_code str_ends_with str_from_char_code str_lower str_pad_left str_pad_right str_repeat str_starts_with "
            "str_trim str_trim_end str_trim_start str_upper time_now time_sleep time_timestamp to_boolean to_json to_number "
            "to_str"
        ).split()
    )
    BRACKET_PAIRS = {"(": ")", "[": "]", "{": "}"}
    BRACKET_SCAN_LIMIT = 20000
    MAX_OUTPUT_LINES = 20000
    IDENTIFIER_START = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
    IDENTIFIER_PART = IDENTIFIER_START + "0123456789"
    OPERATORS = (
        "->",
        "**",
        "<=",
        ">=",
        "<",
        ">",
        "=",
        "+",
        "-",
        "*",
        "/",
        "%",
        "!",
        "~",
    )
    TAB_BAR_BG = "#ececec"
    TAB_ACTIVE_BG = "#ffffff"
    TAB_ACCENT = "#005fb8"
    ANSI_PATTERN = re.compile(r"\x1b\[([0-9;?]*)([A-Za-z])")
    ANSI_COLORS = {
        30: "#666666",
        31: "#cd3131",
        32: "#0dbc79",
        33: "#e5e510",
        34: "#2472c8",
        35: "#bc3fbc",
        36: "#11a8cd",
        37: "#e5e5e5",
        90: "#808080",
        91: "#f14c4c",
        92: "#23d18b",
        93: "#f5f543",
        94: "#3b8eea",
        95: "#d670d6",
        96: "#29b8db",
        97: "#ffffff",
    }

    MAX_FIND_MATCHES = 5000
    FIND_BAR_BG = "#f3f3f3"
    FIND_MATCH_BG = "#fff3b0"
    FIND_CURRENT_BG = "#ffb86b"
    FIND_ERROR_BG = "#fde7e9"
    FALLBACK_FLAGS = {
        "--ast": False,
        "--bytecode": False,
        "--root": True,
        "--max-steps": True,
        "--timeout": True,
        "--no-network": False,
        "--allow-private-network": False,
        "--no-filesystem": False,
        "--hardware": False,
        "--help": False,
    }

    editor = _active("editor")
    line_numbers = _active("line_numbers")
    vertical_scrollbar = _active("vertical_scrollbar")
    horizontal_scrollbar = _active("horizontal_scrollbar")
    current_file_path = _active("path")
    dirty = _active("dirty")
    line_ending = _active("line_ending")
    numbered_lines = _active("numbered_lines")

    def __init__(self, root):
        self.root = root
        self.base_dir = Path(__file__).resolve().parent
        self.engine_path = self.find_engine()
        self.doc = None
        self.docs = []
        self.last_run_doc = None
        self.input_history = []
        self.command_history = []
        self.history_index = 0
        self.history_draft = ""
        self.command_input_active = False
        self.command_override = None
        self.app_icon = None
        self.run_flags = []
        self.shell_cwd = None
        self.flag_cache_key = None
        self.flag_info = {}
        self.engine_help_text = ""
        self.ansi_color = None
        self.ansi_tail = ""
        self.ansi_tags = set()
        self.carriage_return = False
        self.last_search = ""
        self.last_run_path = None
        self.find_visible = False
        self.replace_visible = False
        self.find_matches = []
        self.find_truncated = False
        self.find_job = None
        self.running = False
        self.process = None
        self.pending_input_lines = []
        self.stop_event = None
        self.result_queue = queue.Queue()
        self.stdin_queue = queue.Queue()
        self.worker_thread = None
        self.temp_path = None
        self.temp_arg = None
        self.run_dir = self.base_dir
        self.last_run_source = None
        self.last_run_dir = self.base_dir
        self.highlight_job = None
        self.poll_job = None
        self.font_size = 12
        self.font_family = self.available_monospace_font()
        self.theme = "light"
        self.closing = False
        self.run_started = None
        self.folder = None
        self.tree_paths = {}
        self.sidebar_visible = True

        self.root.title("CuffScript IDE - Untitled")
        self.root.geometry("1080x780")
        self.root.minsize(760, 520)
        self._set_window_icon()
        self.root.protocol("WM_DELETE_WINDOW", self.close_window)
        if sys.platform == "darwin":
            self.root.createcommand("tk::mac::Quit", self.close_window)
        try:
            ttk.Style(self.root).theme_use("clam")
        except tk.TclError:
            pass

        self._build_menu()
        self._build_status_bar()
        self._build_workspace()
        self._build_editor_area()
        self._build_output_area()
        self._bind_events()

        self.update_status()
        self.update_engine_status()
        self.apply_theme(self.theme)
        self._prepare_command_input()

    def available_monospace_font(self):
        if os.name == "nt":
            candidates = (
                "D2Coding",
                "NanumGothicCoding",
                "Cascadia Mono",
                "Consolas",
                "Courier New",
            )
        elif sys.platform == "darwin":
            candidates = ("D2Coding", "NanumGothicCoding", "Menlo", "Monaco", "Courier")
        else:
            candidates = (
                "D2Coding",
                "NanumGothicCoding",
                "DejaVu Sans Mono",
                "Liberation Mono",
                "Noto Sans Mono",
                "Ubuntu Mono",
                "Monospace",
            )
        try:
            available = set(self.root.tk.call("font", "families"))
            for candidate in candidates:
                if candidate in available:
                    return candidate
        except tk.TclError:
            pass
        return "TkFixedFont"

    def find_engine(self):
        configured = os.environ.get("CUFFC_PATH")
        if configured:
            path = Path(configured).expanduser()
            if path.is_file():
                return path.resolve()
        names = ("cuffc.exe", "cuffc") if os.name == "nt" else ("cuffc", "cuffc.exe")
        search_dirs = [self.base_dir, Path.cwd()]
        if getattr(sys, "frozen", False):
            # Packaged app: base_dir is PyInstaller's temp folder, so also look next to the
            # executable (and next to the .app bundle on macOS).
            exe_dir = Path(sys.executable).resolve().parent
            search_dirs.insert(0, exe_dir)
            if sys.platform == "darwin" and exe_dir.parent.name == "Contents":
                search_dirs.insert(1, exe_dir.parents[2])
        for directory in search_dirs:
            for name in names:
                candidate = directory / name
                if candidate.is_file():
                    return candidate.resolve()
        for name in names:
            located = shutil.which(name)
            if located:
                return Path(located).resolve()
        return self.base_dir / names[0]

    def _set_window_icon(self):
        icon_path = self.base_dir / "assets" / "cuffscript_symbol_512.png"
        if not icon_path.is_file():
            return
        try:
            self.app_icon = tk.PhotoImage(master=self.root, file=str(icon_path))
            self.root.iconphoto(True, self.app_icon)
        except (OSError, tk.TclError):
            self.app_icon = None

    def _build_menu(self):
        menubar = tk.Menu(self.root)
        modifier_label = "⌘" if sys.platform == "darwin" else "Ctrl"

        file_menu = tk.Menu(menubar, tearoff=False)
        file_menu.add_command(
            label="New File", accelerator=modifier_label + "+N", command=self.new_file
        )
        file_menu.add_command(
            label="Open...", accelerator=modifier_label + "+O", command=self.open_file
        )
        file_menu.add_command(
            label="Open Folder...",
            accelerator=modifier_label + "+Shift+O",
            command=self.open_folder,
        )
        file_menu.add_separator()
        file_menu.add_command(
            label="Save", accelerator=modifier_label + "+S", command=self.save_file
        )
        file_menu.add_command(
            label="Save As...",
            accelerator=modifier_label + "+Shift+S",
            command=self.save_as_file,
        )
        file_menu.add_command(label="Copy File Path", command=self.copy_file_path)
        file_menu.add_command(
            label="Close Tab",
            accelerator=modifier_label + "+W",
            command=self.close_current_document,
        )
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.close_window)
        menubar.add_cascade(label="File", menu=file_menu)

        edit_menu = tk.Menu(menubar, tearoff=False)
        edit_menu.add_command(
            label="Undo", accelerator=modifier_label + "+Z", command=self.undo
        )
        redo_accelerator = modifier_label + (
            "+Shift+Z" if sys.platform == "darwin" else "+Y"
        )
        edit_menu.add_command(
            label="Redo", accelerator=redo_accelerator, command=self.redo
        )
        edit_menu.add_separator()
        edit_menu.add_command(
            label="Cut", accelerator=modifier_label + "+X", command=self.cut
        )
        edit_menu.add_command(
            label="Copy", accelerator=modifier_label + "+C", command=self.copy
        )
        edit_menu.add_command(
            label="Paste", accelerator=modifier_label + "+V", command=self.paste
        )
        edit_menu.add_command(
            label="Select All",
            accelerator=modifier_label + "+A",
            command=self.select_all,
        )
        edit_menu.add_separator()
        edit_menu.add_command(
            label="Find...", accelerator=modifier_label + "+F", command=self.find_text
        )
        edit_menu.add_command(
            label="Find Next", accelerator="F3", command=self.find_next
        )
        edit_menu.add_command(
            label="Find Previous", accelerator="Shift+F3", command=self.find_previous
        )
        edit_menu.add_command(
            label="Replace...",
            accelerator=modifier_label + "+H",
            command=self.replace_text,
        )
        edit_menu.add_command(
            label="Go to Line...",
            accelerator=modifier_label + "+G",
            command=self.go_to_line,
        )
        edit_menu.add_separator()
        edit_menu.add_command(
            label="Duplicate Line",
            accelerator=modifier_label + "+D",
            command=self.duplicate_line,
        )
        edit_menu.add_command(
            label="Delete Line",
            accelerator=modifier_label + "+Shift+K",
            command=self.delete_line,
        )
        edit_menu.add_command(
            label="Indent", accelerator="Tab", command=self.indent_selection
        )
        edit_menu.add_command(
            label="Outdent", accelerator="Shift+Tab", command=self.remove_indent
        )
        edit_menu.add_command(
            label="Toggle Comment",
            accelerator=modifier_label + "+Shift+C",
            command=self.toggle_comment,
        )
        completion_label = (
            "⌥Space" if sys.platform == "darwin" else modifier_label + "+Space"
        )
        completion_alt = "⌥/" if sys.platform == "darwin" else "Alt+/"
        edit_menu.add_command(
            label="Autocomplete",
            accelerator=f"{completion_label}, {completion_alt}",
            command=self.autocomplete,
        )
        menubar.add_cascade(label="Edit", menu=edit_menu)

        run_menu = tk.Menu(menubar, tearoff=False)
        run_label, stop_label = (
            ("⌘R", "⌘.") if sys.platform == "darwin" else ("F5", "Shift+F5")
        )
        run_menu.add_command(
            label="Run Script", accelerator=run_label, command=self.run_code
        )
        run_menu.add_command(
            label="Stop", accelerator=stop_label, command=self.stop_code
        )
        run_menu.add_separator()
        run_menu.add_command(label="Clear Output", command=self.clear_output)
        menubar.add_cascade(label="Run", menu=run_menu)

        view_menu = tk.Menu(menubar, tearoff=False)
        view_menu.add_command(
            label="Toggle Explorer",
            accelerator=modifier_label + "+B",
            command=self.toggle_sidebar,
        )
        view_menu.add_command(
            label="Next Tab",
            accelerator="Ctrl+Tab",
            command=lambda: self.cycle_document(1),
        )
        view_menu.add_command(
            label="Previous Tab",
            accelerator="Ctrl+Shift+Tab",
            command=lambda: self.cycle_document(-1),
        )
        view_menu.add_separator()
        view_menu.add_command(
            label="Increase Font Size",
            accelerator=modifier_label + "++",
            command=lambda: self.change_font_size(1),
        )
        view_menu.add_command(
            label="Decrease Font Size",
            accelerator=modifier_label + "+-",
            command=lambda: self.change_font_size(-1),
        )
        view_menu.add_command(label="Reset Font Size", command=self.reset_font_size)
        menubar.add_cascade(label="View", menu=view_menu)

        options_menu = tk.Menu(menubar, tearoff=False)
        options_menu.add_command(label="Select Compiler...", command=self.choose_engine)
        options_menu.add_separator()
        self.theme_var = tk.StringVar(value=self.theme)
        theme_menu = tk.Menu(options_menu, tearoff=False)
        for name, label in (("light", "Light"), ("dark", "Dark")):
            theme_menu.add_radiobutton(
                label=label,
                value=name,
                variable=self.theme_var,
                command=lambda selected=name: self.apply_theme(selected),
            )
        options_menu.add_cascade(label="Theme", menu=theme_menu)
        menubar.add_cascade(label="Settings", menu=options_menu)

        help_menu = tk.Menu(menubar, tearoff=False)
        command_reference = tk.Menu(help_menu, tearoff=False)
        for name, (_handler, _usage, description) in self._command_table().items():
            details = tk.Menu(command_reference, tearoff=False)
            details.add_command(label=self._usage_text(name), state=tk.DISABLED)
            details.add_separator()
            details.add_command(label=description, state=tk.DISABLED)
            command_reference.add_cascade(label=f"%{name}", menu=details)
        help_menu.add_cascade(label="IDE Commands", menu=command_reference)
        help_menu.add_separator()
        help_menu.add_command(label="About CuffScript IDE", command=self.show_about)
        menubar.add_cascade(label="Help", menu=help_menu)

        self.root.config(menu=menubar)

    def _build_workspace(self):
        self.main_split = tk.PanedWindow(
            self.root,
            orient=tk.HORIZONTAL,
            sashwidth=4,
            sashrelief=tk.FLAT,
            background="#d8d8d8",
            borderwidth=0,
        )
        self.main_split.pack(fill=tk.BOTH, expand=True)
        self._build_sidebar()

    def _build_sidebar(self):
        panel = "#f3f3f3"
        muted = "#6f6f6f"
        self.sidebar = tk.Frame(self.main_split, background=panel)
        header = tk.Frame(self.sidebar, background=panel)
        header.pack(side=tk.TOP, fill=tk.X, padx=12, pady=(10, 6))
        tk.Label(header, text="Explorer", background=panel, foreground=muted).pack(
            side=tk.LEFT
        )
        for text, command in (
            ("Refresh", self.refresh_tree),
            ("Open Folder", self.open_folder),
        ):
            link = tk.Label(
                header, text=text, background=panel, foreground=muted, cursor="hand2"
            )
            link.pack(side=tk.RIGHT, padx=(10, 0))
            link.bind("<Button-1>", lambda event, action=command: action())
            link.bind(
                "<Enter>",
                lambda event, widget=link: widget.configure(foreground="#005fb8"),
            )
            link.bind(
                "<Leave>", lambda event, widget=link: widget.configure(foreground=muted)
            )
        tk.Frame(self.sidebar, background="#e0e0e0", height=1).pack(
            side=tk.TOP, fill=tk.X
        )

        body = tk.Frame(self.sidebar, background=panel)
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        self._make_icons()
        style = ttk.Style(self.root)
        style.configure(
            "Explorer.Treeview",
            background=panel,
            fieldbackground=panel,
            foreground="#333333",
            borderwidth=0,
            rowheight=24,
        )
        style.map(
            "Explorer.Treeview",
            background=[("selected", "#e4e6f1")],
            foreground=[("selected", "#000000")],
        )
        try:
            style.layout(
                "Explorer.Treeview", [("Treeview.treearea", {"sticky": "nswe"})]
            )
        except tk.TclError:
            pass
        self.tree_view = tk.Frame(body, background=panel)
        self.tree = ttk.Treeview(
            self.tree_view, show="tree", selectmode="browse", style="Explorer.Treeview"
        )
        tree_scrollbar = ttk.Scrollbar(
            self.tree_view,
            orient=tk.VERTICAL,
            command=self.tree.yview,
            style="IDE.Vertical.TScrollbar",
        )
        scrollbar_shown = [False]

        def on_tree_scroll(first, last):
            tree_scrollbar.set(first, last)
            needed = float(first) > 0.0 or float(last) < 1.0
            if needed and not scrollbar_shown[0]:
                tree_scrollbar.pack(side=tk.RIGHT, fill=tk.Y, before=self.tree)
                scrollbar_shown[0] = True
            elif not needed and scrollbar_shown[0]:
                tree_scrollbar.pack_forget()
                scrollbar_shown[0] = False

        self.tree.configure(yscrollcommand=on_tree_scroll)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.tree.bind("<<TreeviewOpen>>", self.on_tree_open)
        self.tree.bind("<Double-Button-1>", self.on_tree_double_click)
        self.tree.bind("<Return>", self.on_tree_return)

        self.empty_view = tk.Frame(body, background=panel)
        tk.Label(
            self.empty_view,
            text="No folder is open",
            background=panel,
            foreground=muted,
        ).pack(pady=(32, 10))
        self.empty_folder_button = tk.Button(
            self.empty_view,
            text="Open Folder",
            command=self.open_folder,
            relief=tk.FLAT,
            borderwidth=0,
            padx=12,
            pady=6,
            background="#f0f0f0",
            foreground="#24292f",
            activebackground="#e4e4e4",
            activeforeground="#24292f",
        )
        self.empty_folder_button.pack()
        self.empty_view.pack(fill=tk.BOTH, expand=True)
        self.main_split.add(self.sidebar, width=240, minsize=150, stretch="never")

    def _make_icons(self):
        def draw(shapes):
            image = tk.PhotoImage(master=self.root, width=16, height=16)
            for color, x1, y1, x2, y2 in shapes:
                image.put(color, to=(x1, y1, x2, y2))
            return image

        def page(edge, fill, line):
            return draw(
                [
                    (edge, 3, 1, 13, 15),
                    (fill, 4, 2, 12, 14),
                    (line, 5, 5, 11, 6),
                    (line, 5, 8, 11, 9),
                    (line, 5, 11, 9, 12),
                ]
            )

        self.icon_folder = draw(
            [
                ("#c9962b", 1, 3, 7, 5),
                ("#c9962b", 1, 4, 15, 13),
                ("#f1c65a", 2, 5, 14, 12),
            ]
        )
        self.icon_file = page("#8a8a8a", "#ffffff", "#b0b0b0")
        self.icon_cuff = page("#7b3fb5", "#f1e8fa", "#7b3fb5")

    def _icon_for(self, path, is_dir):
        if is_dir:
            return self.icon_folder
        return self.icon_cuff if path.suffix.lower() == ".cuff" else self.icon_file

    def toggle_sidebar(self, event=None):
        if self.sidebar_visible:
            self.main_split.forget(self.sidebar)
        else:
            self.main_split.add(
                self.sidebar, before=self.paned, width=240, minsize=150, stretch="never"
            )
        self.sidebar_visible = not self.sidebar_visible
        return "break"

    def open_folder(self, event=None):
        start = self.folder or (
            self.current_file_path.parent if self.current_file_path else self.base_dir
        )
        chosen = filedialog.askdirectory(
            parent=self.root, title="Open Folder", initialdir=str(start)
        )
        if chosen:
            self.set_folder(Path(chosen))
        return "break"

    def set_folder(self, path):
        self.folder = path.resolve()
        self.refresh_tree()

    @staticmethod
    def _is_dir(path):
        try:
            return path.is_dir()
        except OSError:
            return False

    def list_directory(self, path):
        try:
            entries = [
                entry
                for entry in path.iterdir()
                if not entry.name.startswith(".") and entry.name != "__pycache__"
            ]
        except OSError:
            return []
        entries.sort(key=lambda entry: (not self._is_dir(entry), entry.name.lower()))
        return entries[:2000]

    def populate_tree(self, item, expanded=frozenset()):
        self.tree.delete(*self.tree.get_children(item))
        for child in self.list_directory(self.tree_paths[item]):
            is_dir = self._is_dir(child)
            child_item = self.tree.insert(
                item, tk.END, text=child.name, image=self._icon_for(child, is_dir)
            )
            self.tree_paths[child_item] = child
            if not is_dir:
                continue
            if child in expanded:
                self.tree.item(child_item, open=True)
                self.populate_tree(child_item, expanded)
            else:
                self.tree.insert(child_item, tk.END, text="")

    def refresh_tree(self):
        expanded = {
            path
            for item, path in self.tree_paths.items()
            if self.tree.exists(item) and self.tree.item(item, "open")
        }
        self.tree.delete(*self.tree.get_children())
        self.tree_paths.clear()
        if self.folder is not None and not self._is_dir(self.folder):
            self.folder = None
        if self.folder is None:
            self.tree_view.pack_forget()
            self.empty_view.pack(fill=tk.BOTH, expand=True)
            return
        self.empty_view.pack_forget()
        self.tree_view.pack(fill=tk.BOTH, expand=True)
        root_item = self.tree.insert(
            "",
            tk.END,
            text=self.folder.name or str(self.folder),
            open=True,
            image=self.icon_folder,
        )
        self.tree_paths[root_item] = self.folder
        self.populate_tree(root_item, expanded)

    def on_tree_open(self, event=None):
        item = self.tree.focus()
        children = self.tree.get_children(item)
        if len(children) == 1 and children[0] not in self.tree_paths:
            self.populate_tree(item)

    def on_tree_double_click(self, event):
        path = self.tree_paths.get(self.tree.identify_row(event.y))
        if path is None or self._is_dir(path):
            return None
        self.open_path(path)
        return "break"

    def on_tree_return(self, event=None):
        item = self.tree.focus()
        path = self.tree_paths.get(item)
        if path is None:
            return "break"
        if self._is_dir(path):
            opened = not self.tree.item(item, "open")
            self.tree.item(item, open=opened)
            if opened:
                self.on_tree_open()
        else:
            self.open_path(path)
        return "break"

    def select_in_tree(self, path):
        for item, candidate in self.tree_paths.items():
            if candidate == path and self.tree.exists(item):
                self.tree.selection_set(item)
                self.tree.see(item)
                return

    def _build_editor_area(self):
        self.paned = tk.PanedWindow(
            self.main_split,
            orient=tk.VERTICAL,
            sashwidth=6,
            sashrelief=tk.RAISED,
            background="#d8d8d8",
            borderwidth=0,
        )
        self.main_split.add(self.paned, minsize=300, stretch="always")

        editor_pane = tk.Frame(self.paned, background="#ffffff")
        tab_bar = tk.Frame(editor_pane, background=self.TAB_BAR_BG)
        tab_bar.pack(side=tk.TOP, fill=tk.X)
        self.tab_list_button = tk.Label(
            tab_bar,
            text="▾",
            background=self.TAB_BAR_BG,
            foreground="#6f6f6f",
            padx=10,
            pady=6,
            cursor="hand2",
        )
        self.tab_list_button.pack(side=tk.RIGHT)
        self.tab_list_button.bind("<Button-1>", self.show_tab_menu)
        self.tab_area = tk.Frame(tab_bar, background=self.TAB_BAR_BG)
        self.tab_area.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.tab_canvas = tk.Canvas(
            self.tab_area,
            height=34,
            background=self.TAB_BAR_BG,
            borderwidth=0,
            highlightthickness=0,
        )
        self.tab_canvas.pack(side=tk.TOP, fill=tk.X, expand=True)
        self.tab_scrollbar = ttk.Scrollbar(
            self.tab_area,
            orient=tk.HORIZONTAL,
            command=self.tab_canvas.xview,
            style="IDE.Horizontal.TScrollbar",
        )
        self.tab_canvas.configure(xscrollcommand=self.tab_scrollbar.set)
        self.tab_strip = tk.Frame(self.tab_canvas, background=self.TAB_BAR_BG)
        self.tab_window = self.tab_canvas.create_window(
            0, 0, anchor="nw", window=self.tab_strip
        )
        self.tab_strip.bind("<Configure>", self.update_tab_scroll_region)
        self.tab_canvas.bind("<Configure>", self.update_tab_scroll_region)
        self.tab_canvas.bind("<Map>", self.update_tab_scroll_region)
        self.tab_canvas.bind("<Shift-MouseWheel>", self.scroll_tabs)
        self.tab_canvas.bind("<Button-4>", self.scroll_tabs)
        self.tab_canvas.bind("<Button-5>", self.scroll_tabs)
        tk.Frame(editor_pane, height=1, background="#d8d8d8").pack(
            side=tk.TOP, fill=tk.X
        )
        self.doc_container = tk.Frame(editor_pane, background="#ffffff")
        self.doc_container.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.paned.add(editor_pane, minsize=180)
        self._build_find_bar(editor_pane)
        self.select_document(self._create_document())

    def update_tab_scroll_region(self, event=None):
        if not hasattr(self, "tab_canvas"):
            return
        content_width = sum(doc.tab.winfo_reqwidth() + 2 for doc in self.docs)
        canvas_width = self.tab_canvas.winfo_width()
        scroll_width = max(content_width, canvas_width)
        content_height = max(34, self.tab_strip.winfo_reqheight())
        self.tab_canvas.itemconfigure(self.tab_window, width=scroll_width)
        self.tab_canvas.configure(scrollregion=(0, 0, scroll_width, content_height))
        overflowing = content_width > canvas_width > 1
        if overflowing and not self.tab_scrollbar.winfo_ismapped():
            self.tab_scrollbar.pack(side=tk.BOTTOM, fill=tk.X)
        elif not overflowing and self.tab_scrollbar.winfo_ismapped():
            self.tab_scrollbar.pack_forget()

    def scroll_tabs(self, event):
        if getattr(event, "num", None) == 4:
            amount = -3
        elif getattr(event, "num", None) == 5:
            amount = 3
        else:
            amount = -int(event.delta / 120) if event.delta else 0
            if amount == 0 and event.delta:
                amount = -1 if event.delta > 0 else 1
        self.tab_canvas.xview_scroll(amount, "units")
        return "break"

    def _create_document(self):
        doc = Document()
        used = {item.untitled_number for item in self.docs}
        number = 1
        while number in used:
            number += 1
        doc.untitled_number = number

        dark = self.theme == "dark"
        editor_background = "#1e1e1e" if dark else "#ffffff"
        editor_foreground = "#d4d4d4" if dark else "#24292f"
        doc.frame = tk.Frame(self.doc_container, background=editor_background)
        font = (self.font_family, self.font_size)
        # The gutter must have exactly the editor's vertical metrics (same pady, no focus ring, and
        # the same height as the editor, which loses a scrollbar's height at the bottom). Scroll
        # positions are fractions of the total height, so any difference grows with the file.
        gutter_holder = tk.Frame(doc.frame, background=editor_background)
        gutter_holder.pack(side=tk.LEFT, fill=tk.Y)
        gutter_spacer = tk.Frame(
            gutter_holder, background="#252526" if dark else "#f2f3f5"
        )
        gutter_spacer.pack(side=tk.BOTTOM, fill=tk.X)
        doc.line_numbers = tk.Text(
            gutter_holder,
            width=4,
            padx=8,
            pady=8,
            highlightthickness=0,
            takefocus=False,
            borderwidth=0,
            background="#252526" if dark else "#f2f3f5",
            foreground="#858585" if dark else "#9299a3",
            state=tk.DISABLED,
            font=font,
            wrap=tk.NONE,
            cursor="arrow",
        )
        doc.line_numbers.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        doc.gutter_spacer = gutter_spacer

        text_frame = tk.Frame(doc.frame, background=editor_background)
        text_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        doc.horizontal_scrollbar = ttk.Scrollbar(
            text_frame, orient=tk.HORIZONTAL, style="IDE.Horizontal.TScrollbar"
        )
        doc.vertical_scrollbar = ttk.Scrollbar(
            doc.frame,
            orient=tk.VERTICAL,
            command=lambda *args, d=doc: self.sync_scroll(d, *args),
            style="IDE.Vertical.TScrollbar",
        )
        doc.editor = tk.Text(
            text_frame,
            wrap=tk.NONE,
            font=font,
            undo=True,
            autoseparators=True,
            maxundo=500,
            borderwidth=0,
            highlightthickness=0,
            padx=8,
            pady=8,
            background=editor_background,
            foreground=editor_foreground,
            insertbackground=editor_foreground,
            selectbackground="#264f78" if dark else "#cce5ff",
            selectforeground="#ffffff" if dark else "#172b4d",
            yscrollcommand=lambda first, last, d=doc: self.on_editor_scroll(
                d, first, last
            ),
            xscrollcommand=doc.horizontal_scrollbar.set,
            tabs=(self._tab_width(),),
        )
        doc.editor.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        doc.horizontal_scrollbar.configure(command=doc.editor.xview)
        doc.horizontal_scrollbar.pack(side=tk.BOTTOM, fill=tk.X)
        doc.horizontal_scrollbar.update_idletasks()
        gutter_spacer.configure(height=doc.horizontal_scrollbar.winfo_reqheight())
        doc.vertical_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self._configure_syntax_tags(doc.editor)
        doc.editor.bind("<Button-3>", self.show_editor_menu)
        if sys.platform == "darwin":
            doc.editor.bind("<Button-2>", self.show_editor_menu)
            doc.editor.bind("<Control-Button-1>", self.show_editor_menu)

        idle = self.TAB_BAR_BG
        doc.tab = tk.Frame(self.tab_strip, background=idle, cursor="hand2")
        doc.tab_accent = tk.Frame(doc.tab, height=2, background=idle)
        doc.tab_accent.pack(side=tk.TOP, fill=tk.X)
        doc.tab_title = tk.Label(doc.tab, background=idle, pady=5)
        doc.tab_title.pack(side=tk.LEFT, padx=(10, 2))
        doc.tab_close = tk.Label(doc.tab, text="×", background=idle, padx=5)
        doc.tab_close.pack(side=tk.LEFT, padx=(0, 5))
        middle_button = "<Button-3>" if sys.platform == "darwin" else "<Button-2>"
        for widget in (doc.tab, doc.tab_accent, doc.tab_title):
            widget.bind("<Button-1>", lambda event, d=doc: self.select_document(d))
            widget.bind(middle_button, lambda event, d=doc: self.close_document(d))
        doc.tab_close.bind("<Button-1>", lambda event, d=doc: self.close_document(d))
        doc.tab_close.bind(
            "<Enter>", lambda event, d=doc: d.tab_close.configure(text="×")
        )
        doc.tab_close.bind("<Leave>", lambda event, d=doc: self._refresh_tab(d))
        doc.tab.bind("<Configure>", self.update_tab_scroll_region)
        doc.tab.pack(side=tk.LEFT, padx=(0, 1))

        self.docs.append(doc)
        self._bind_editor(doc)
        self._refresh_tab(doc)
        self.update_tab_scroll_region()
        return doc

    def _build_output_area(self):
        output_frame = tk.Frame(self.paned, background="#1e1e1e")
        header = tk.Frame(output_frame, background="#252526")
        header.pack(side=tk.TOP, fill=tk.X)
        self.output_header_title = tk.Label(
            header,
            text="Terminal",
            background="#252526",
            foreground="#e7e7e7",
            padx=12,
            pady=5,
        )
        self.output_header_title.pack(side=tk.LEFT)
        self.output_header = header
        clear_label = tk.Label(
            header,
            text="Clear",
            background="#252526",
            foreground="#9d9d9d",
            padx=12,
            cursor="hand2",
        )
        self.output_clear_label = clear_label
        clear_label.pack(side=tk.RIGHT)
        clear_label.bind("<Button-1>", lambda event: self.clear_output())
        body = tk.Frame(output_frame, background="#1e1e1e")
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.output = tk.Text(
            body,
            wrap=tk.CHAR,
            font=(self.font_family, 11),
            background="#1e1e1e",
            foreground="#cccccc",
            insertbackground="#1e1e1e",
            insertofftime=0,
            selectbackground="#264f78",
            selectforeground="#ffffff",
            borderwidth=0,
            highlightthickness=0,
            padx=10,
            pady=6,
            undo=False,
            cursor="xterm",
        )
        try:
            self.output.configure(blockcursor=True)
        except tk.TclError:
            pass
        self.output.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        output_scrollbar = ttk.Scrollbar(
            body,
            orient=tk.VERTICAL,
            command=self.output.yview,
            style="IDE.Vertical.TScrollbar",
        )
        output_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.output.config(yscrollcommand=output_scrollbar.set)
        self.output.tag_configure("error", foreground="#f48771")
        self.output.tag_configure("prompt", foreground="#23d18b")
        self.output.tag_configure("success", foreground="#cccccc")
        self.output.tag_configure("input", foreground="#cccccc")
        self.output.tag_configure("hint", foreground="#808080")
        self.terminal_menu = tk.Menu(self.output, tearoff=False)
        self.terminal_menu.add_command(label="Copy", command=self.terminal_copy)
        self.terminal_menu.add_command(label="Paste", command=self.terminal_paste)
        self.terminal_menu.add_command(
            label="Select All",
            command=lambda: self.terminal_select_all(everything=True),
        )
        self.terminal_menu.add_separator()
        self.terminal_menu.add_command(label="Clear", command=self.clear_output)
        self.paned.add(output_frame, minsize=135)

    def _build_status_bar(self):
        status_frame = ttk.Frame(self.root, padding=(10, 5))
        status_frame.pack(side=tk.BOTTOM, fill=tk.X)
        self.status_var = tk.StringVar(value="Ready")
        self.position_var = tk.StringVar(value="Ln 1, Col 1")
        self.engine_var = tk.StringVar(value="Checking compiler")
        ttk.Label(status_frame, textvariable=self.status_var, anchor="w").pack(
            side=tk.LEFT, fill=tk.X, expand=True
        )
        ttk.Label(status_frame, textvariable=self.engine_var, anchor="e").pack(
            side=tk.RIGHT, padx=(8, 12)
        )
        ttk.Label(status_frame, textvariable=self.position_var, anchor="e").pack(
            side=tk.RIGHT
        )

    def _comment_font(self):
        """Italic comments, unless the italic face is taller than the regular one: a taller line
        would push the code below out of step with the line-number gutter."""
        italic = (self.font_family, self.font_size, "italic")
        try:
            regular = tkfont.Font(family=self.font_family, size=self.font_size)
            slanted = tkfont.Font(
                family=self.font_family, size=self.font_size, slant="italic"
            )
            if regular.metrics("linespace") != slanted.metrics("linespace"):
                return (self.font_family, self.font_size)
        except Exception:
            pass
        return italic

    def _configure_syntax_tags(self, editor):
        dark = self.theme == "dark"
        editor.tag_configure("keyword", foreground="#c586c0" if dark else "#7b3fb5")
        editor.tag_configure("builtin", foreground="#4ec9b0" if dark else "#087e8b")
        editor.tag_configure("type", foreground="#dcdcaa" if dark else "#986801")
        editor.tag_configure("string", foreground="#ce9178" if dark else "#188038")
        editor.tag_configure("pattern", foreground="#d7aefb" if dark else "#a142f4")
        editor.tag_configure(
            "comment",
            foreground="#7f848e" if dark else "#8c959f",
            font=self._comment_font(),
        )
        editor.tag_configure("number", foreground="#b5cea8" if dark else "#b05a00")
        editor.tag_configure("function", foreground="#9cdcfe" if dark else "#005cc5")
        editor.tag_configure("operator", foreground="#d4d4d4" if dark else "#d14b72")
        editor.tag_configure("error_line", background="#4b2929" if dark else "#fff1f0")
        editor.tag_configure(
            "bracket_match",
            background="#3a3d41" if dark else "#e8eef7",
            foreground="#ffffff" if dark else "#111111",
        )
        editor.tag_configure("search_match", background=self.FIND_MATCH_BG)
        editor.tag_configure("search_current", background=self.FIND_CURRENT_BG)

    def apply_theme(self, theme):
        self.theme = theme if theme in ("light", "dark") else "light"
        if hasattr(self, "theme_var"):
            self.theme_var.set(self.theme)
        dark = self.theme == "dark"
        backgrounds = (
            {
                "#ffffff": "#1e1e1e",
                "#f2f3f5": "#252526",
                "#ececec": "#2d2d30",
                "#f3f3f3": "#252526",
                "#d8d8d8": "#3e3e42",
                "#e0e0e0": "#3e3e42",
                "#252526": "#252526",
                "#1e1e1e": "#1e1e1e",
            }
            if dark
            else {
                "#1e1e1e": "#ffffff",
                "#252526": "#f3f3f3",
                "#2d2d30": "#ececec",
                "#3e3e42": "#d8d8d8",
            }
        )
        foregrounds = (
            {
                "#24292f": "#d4d4d4",
                "#222222": "#d4d4d4",
                "#333333": "#d4d4d4",
                "#6f6f6f": "#a0a0a0",
                "#9299a3": "#858585",
                "#000000": "#ffffff",
            }
            if dark
            else {"#d4d4d4": "#24292f", "#a0a0a0": "#6f6f6f", "#858585": "#9299a3"}
        )

        def recolor(widget):
            for option, colors in (
                ("background", backgrounds),
                ("foreground", foregrounds),
            ):
                try:
                    current = str(widget.cget(option)).lower()
                    if current in colors:
                        widget.configure(**{option: colors[current]})
                except (tk.TclError, TypeError):
                    pass
            if isinstance(widget, tk.Text):
                try:
                    widget.configure(
                        insertbackground="#d4d4d4" if dark else "#24292f",
                        selectbackground="#264f78" if dark else "#cce5ff",
                        selectforeground="#ffffff" if dark else "#172b4d",
                    )
                except tk.TclError:
                    pass
            for child in widget.winfo_children():
                recolor(child)

        recolor(self.root)
        self.root.configure(background="#1e1e1e" if dark else "#ffffff")
        self.TAB_BAR_BG = "#2d2d30" if dark else "#ececec"
        self.TAB_ACTIVE_BG = "#1e1e1e" if dark else "#ffffff"
        self.FIND_BAR_BG = "#252526" if dark else "#f3f3f3"
        self.FIND_MATCH_BG = "#614a18" if dark else "#fff3b0"
        self.FIND_CURRENT_BG = "#805b30" if dark else "#ffb86b"
        self.FIND_ERROR_BG = "#4b2929" if dark else "#fde7e9"
        ttk_style = ttk.Style(self.root)
        ttk_style.configure("TFrame", background="#252526" if dark else "#f3f3f3")
        ttk_style.configure(
            "TLabel",
            background="#252526" if dark else "#f3f3f3",
            foreground="#d4d4d4" if dark else "#24292f",
        )
        ttk_style.configure(
            "TButton",
            background="#3c3c3c" if dark else "#f0f0f0",
            foreground="#f1f1f1" if dark else "#24292f",
        )
        ttk_style.map(
            "TButton",
            background=[("active", "#505050" if dark else "#e4e4e4")],
        )
        ttk_style.configure(
            "TCheckbutton",
            background="#252526" if dark else "#f3f3f3",
            foreground="#d4d4d4" if dark else "#24292f",
        )
        ttk_style.configure(
            "Toolbutton",
            background="#252526" if dark else "#f3f3f3",
            foreground="#d4d4d4" if dark else "#24292f",
        )
        scrollbar_background = "#555555" if dark else "#c4c7cc"
        scrollbar_trough = "#252526" if dark else "#f0f1f3"
        scrollbar_border = "#252526" if dark else "#e2e4e8"
        for style_name in ("IDE.Vertical.TScrollbar", "IDE.Horizontal.TScrollbar"):
            ttk_style.configure(
                style_name,
                background=scrollbar_background,
                darkcolor=scrollbar_background,
                lightcolor=scrollbar_background,
                troughcolor=scrollbar_trough,
                bordercolor=scrollbar_border,
                arrowcolor="#d4d4d4" if dark else "#62666d",
                borderwidth=0,
                relief=tk.FLAT,
            )
            ttk_style.map(
                style_name,
                background=[
                    ("active", "#707070" if dark else "#aeb3ba"),
                    ("pressed", "#858585" if dark else "#9298a1"),
                ],
            )
        ttk_style.configure(
            "Explorer.Treeview",
            background="#252526" if dark else "#f3f3f3",
            fieldbackground="#252526" if dark else "#f3f3f3",
            foreground="#d4d4d4" if dark else "#333333",
        )
        ttk_style.map(
            "Explorer.Treeview",
            background=[("selected", "#37373d" if dark else "#e4e6f1")],
            foreground=[("selected", "#ffffff" if dark else "#000000")],
        )
        if hasattr(self, "tab_strip"):
            self.tab_strip.configure(background=self.TAB_BAR_BG)
            self.tab_area.configure(background=self.TAB_BAR_BG)
            self.tab_canvas.configure(background=self.TAB_BAR_BG)
            self.tab_list_button.configure(
                background=self.TAB_BAR_BG,
                foreground="#a0a0a0" if dark else "#6f6f6f",
            )
            self.update_tab_scroll_region()
            for doc in self.docs:
                self._refresh_tab(doc)
                doc.gutter_spacer.configure(background="#252526" if dark else "#f2f3f5")
                doc.line_numbers.configure(
                    background="#252526" if dark else "#f2f3f5",
                    foreground="#858585" if dark else "#9299a3",
                )
                doc.editor.configure(
                    background="#1e1e1e" if dark else "#ffffff",
                    foreground="#d4d4d4" if dark else "#24292f",
                )
                self._configure_syntax_tags(doc.editor)
            if hasattr(self, "output"):
                self.output.configure(
                    background="#1e1e1e" if dark else "#1e1e1e",
                    foreground="#cccccc",
                )
                self.output_header.configure(background="#252526")
                self.output_header_title.configure(
                    background="#252526", foreground="#e7e7e7"
                )
                self.output_clear_label.configure(
                    background="#252526", foreground="#9d9d9d"
                )
            if hasattr(self, "find_bar"):
                self.find_bar.configure(background=self.FIND_BAR_BG)
                self.replace_row.configure(background=self.FIND_BAR_BG)
            if hasattr(self, "empty_folder_button"):
                self.empty_folder_button.configure(
                    background="#3c3c3c" if dark else "#f0f0f0",
                    foreground="#f1f1f1" if dark else "#24292f",
                    activebackground="#505050" if dark else "#e4e4e4",
                    activeforeground="#ffffff" if dark else "#24292f",
                )

    def _global_action(self, key, shift):
        table = {
            ("n", False): self.new_file,
            ("o", False): self.open_file,
            ("o", True): self.open_folder,
            ("s", False): self.save_file,
            ("s", True): self.save_as_file,
            ("w", False): self.close_current_document,
            ("b", False): self.toggle_sidebar,
        }
        return table.get((key, shift))

    def _bind_events(self):
        output = self.output
        output.bind("<Double-Button-1>", self.go_to_error_line)
        output.bind("<Motion>", self.on_output_motion)
        output.bind("<KeyPress>", self.on_terminal_keypress)
        output.bind("<Return>", self.submit_terminal_input)
        output.bind("<KP_Enter>", self.submit_terminal_input)
        output.bind("<BackSpace>", self.terminal_backspace)
        output.bind("<Button-1>", self.terminal_click)
        output.bind("<<Paste>>", self.terminal_paste)
        if sys.platform == "darwin":
            output.bind("<Command-c>", self.terminal_copy)
            output.bind("<Command-a>", self.terminal_select_all)
            output.bind("<Command-v>", self.terminal_paste)
            output.bind("<Button-2>", self.show_terminal_menu)
            output.bind("<Control-Button-1>", self.show_terminal_menu)
        else:
            output.bind("<Button-3>", self.show_terminal_menu)
            # The default middle-click pastes into the text at the pointer, which would corrupt the output.
            output.bind("<Button-2>", lambda event: "break")
            output.bind("<ButtonRelease-2>", lambda event: "break")

        modifier = "Command" if sys.platform == "darwin" else "Control"
        self.root.bind("<F5>", lambda event: self.run_code())
        self.root.bind("<F1>", lambda event: self.show_about())
        self.root.bind("<Shift-F5>", lambda event: self.stop_code())
        if sys.platform == "darwin":
            self.root.bind("<Command-r>", lambda event: self.run_code())
            self.root.bind("<Command-period>", lambda event: self.stop_code())
        self.root.bind(f"<{modifier}-plus>", lambda event: self.change_font_size(1))
        self.root.bind(f"<{modifier}-equal>", lambda event: self.change_font_size(1))
        self.root.bind(f"<{modifier}-minus>", lambda event: self.change_font_size(-1))
        for (key, shift), action in (
            (("n", False), self.new_file),
            (("o", False), self.open_file),
            (("o", True), self.open_folder),
            (("s", False), self.save_file),
            (("s", True), self.save_as_file),
            (("w", False), self.close_current_document),
            (("b", False), self.toggle_sidebar),
        ):
            letter = key.upper() if shift else key
            sequence = (
                f"<{modifier}-Shift-{letter}>" if shift else f"<{modifier}-{letter}>"
            )
            self.root.bind(sequence, lambda event, act=action: (act(), "break")[1])
        for sequence, step in self._tab_cycle_sequences():
            try:
                self.root.bind(sequence, lambda event, s=step: self.cycle_document(s))
            except tk.TclError:
                pass

    @staticmethod
    def _tab_cycle_sequences():
        return (
            ("<Control-Tab>", 1),
            ("<Control-Shift-Tab>", -1),
            ("<Control-ISO_Left_Tab>", -1),
            ("<Control-Next>", 1),
            ("<Control-Prior>", -1),
        )

    def _bind_editor(self, doc):
        editor = doc.editor
        editor.bind("<<Modified>>", lambda event, d=doc: self.on_modified(d))
        editor.bind("<KeyRelease>", self.on_key_release)
        editor.bind("<ButtonRelease-1>", self.on_key_release)
        for widget in (editor, doc.line_numbers):
            widget.bind(
                "<MouseWheel>", lambda event, d=doc: self.on_mouse_wheel(event, d)
            )
            widget.bind(
                "<Button-4>", lambda event, d=doc: self.on_linux_wheel(event, d)
            )
            widget.bind(
                "<Button-5>", lambda event, d=doc: self.on_linux_wheel(event, d)
            )

        # Click or drag in the line-number gutter to select whole lines.
        gutter = doc.line_numbers
        for sequence in ("<Button-1>", "<Double-Button-1>", "<Triple-Button-1>"):
            gutter.bind(sequence, lambda event, d=doc: self.gutter_press(event, d))
        gutter.bind("<B1-Motion>", lambda event, d=doc: self.gutter_drag(event, d))
        gutter.bind(
            "<ButtonRelease-1>", lambda event, d=doc: self.gutter_release(event, d)
        )

        modifier = "Command" if sys.platform == "darwin" else "Control"

        def shortcut(action):
            def handler(event):
                action()
                return "break"

            return handler

        editor_shortcuts = {
            f"<{modifier}-n>": self.new_file,
            f"<{modifier}-o>": self.open_file,
            f"<{modifier}-s>": self.save_file,
            f"<{modifier}-Shift-S>": self.save_as_file,
            f"<{modifier}-w>": self.close_current_document,
            f"<{modifier}-x>": self.cut,
            f"<{modifier}-c>": self.copy,
            f"<{modifier}-v>": self.paste,
            f"<{modifier}-f>": self.find_text,
            f"<{modifier}-h>": self.replace_text,
            f"<{modifier}-g>": self.go_to_line,
            f"<{modifier}-a>": self.select_all,
            f"<{modifier}-d>": self.duplicate_line,
            f"<{modifier}-Shift-K>": self.delete_line,
            f"<{modifier}-Shift-C>": self.toggle_comment,
            (
                "<Option-space>" if sys.platform == "darwin" else f"<{modifier}-space>"
            ): self.autocomplete,
            (
                "<Option-slash>" if sys.platform == "darwin" else "<Alt-slash>"
            ): self.autocomplete,
            f"<{modifier}-b>": self.toggle_sidebar,
            f"<{modifier}-Shift-O>": self.open_folder,
            "<F3>": self.find_next,
            "<Shift-F3>": self.find_previous,
            "<Escape>": self.close_find_bar,
            f"<{modifier}-z>": self.undo,
            (
                f"<{modifier}-Shift-z>"
                if sys.platform == "darwin"
                else f"<{modifier}-y>"
            ): self.redo,
        }
        for sequence, callback in editor_shortcuts.items():
            editor.bind(sequence, shortcut(callback))
        for sequence, step in self._tab_cycle_sequences():
            try:
                editor.bind(sequence, lambda event, s=step: self.cycle_document(s))
            except tk.TclError:
                pass
        editor.bind("<Tab>", self.insert_indent)
        editor.bind("<Shift-Tab>", self.remove_indent)
        editor.bind("<Return>", self.insert_newline)
        editor.bind("<BackSpace>", self.smart_backspace)

    def _set_output(self, text="", tag=None, append=False):
        if not hasattr(self, "output"):
            return
        if not append:
            self.output.delete("1.0", tk.END)
            self.carriage_return = False
        if text:
            self.output.insert(tk.END, text, tag or ())
        self.output.see(tk.END)
        self.output.mark_set(tk.INSERT, tk.END)

    def set_terminal_cursor(self, active):
        self.output.configure(insertbackground="#cccccc" if active else "#1e1e1e")

    def clear_output(self):
        return self.terminal_clear_screen()

    @staticmethod
    def _gutter_line(doc, event):
        last = int(doc.editor.index("end-1c").split(".")[0])
        try:
            line = int(doc.line_numbers.index(f"@0,{event.y}").split(".")[0])
        except (tk.TclError, ValueError):
            line = last
        return max(1, min(line, last)), last

    @staticmethod
    def _select_lines(doc, anchor, line, last):
        editor = doc.editor
        first, final = min(anchor, line), max(anchor, line)
        start = f"{first}.0"
        end = "end-1c" if final >= last else f"{final + 1}.0"
        editor.tag_remove("sel", "1.0", tk.END)
        editor.tag_add("sel", start, end)
        editor.mark_set(tk.INSERT, end if final >= last or line >= anchor else start)

    def gutter_press(self, event, doc):
        line, last = self._gutter_line(doc, event)
        if event.state & 0x1 and doc.gutter_anchor is not None:
            anchor = doc.gutter_anchor  # Shift+click extends from the previous click
        else:
            anchor = doc.gutter_anchor = line
        doc.editor.focus_set()
        self._select_lines(doc, anchor, line, last)
        return "break"

    def gutter_drag(self, event, doc):
        if doc.gutter_anchor is None:
            return "break"
        height = doc.line_numbers.winfo_height()
        if event.y < 0:
            doc.editor.yview_scroll(-1, "units")
        elif event.y > height:
            doc.editor.yview_scroll(1, "units")
        doc.line_numbers.yview_moveto(doc.editor.yview()[0])
        line, last = self._gutter_line(doc, event)
        if event.y < 0:
            line = int(doc.editor.index("@0,0").split(".")[0])
        elif event.y > height:
            line = int(doc.editor.index(f"@0,{height}").split(".")[0])
        self._select_lines(doc, doc.gutter_anchor, max(1, min(line, last)), last)
        return "break"

    def gutter_release(self, event, doc):
        self.on_key_release(event)
        return "break"

    def sync_scroll(self, doc, *args):
        doc.editor.yview(*args)
        doc.line_numbers.yview(*args)

    def on_editor_scroll(self, doc, first, last):
        doc.vertical_scrollbar.set(first, last)
        doc.line_numbers.yview_moveto(first)

    @staticmethod
    def _wheel_units(event):
        if not event.delta:
            return 0
        if sys.platform == "darwin":
            return -1 if event.delta > 0 else 1
        steps = int(event.delta / 120)
        if steps == 0:
            steps = 1 if event.delta > 0 else -1
        return -steps

    def on_mouse_wheel(self, event, doc):
        units = self._wheel_units(event)
        if event.state & 0x1:
            doc.editor.xview_scroll(units, "units")
        else:
            doc.editor.yview_scroll(units, "units")
            doc.line_numbers.yview_moveto(doc.editor.yview()[0])
        return "break"

    def on_linux_wheel(self, event, doc):
        units = -3 if event.num == 4 else 3
        if event.state & 0x1:
            doc.editor.xview_scroll(units, "units")
        else:
            doc.editor.yview_scroll(units, "units")
            doc.line_numbers.yview_moveto(doc.editor.yview()[0])
        return "break"

    def show_editor_menu(self, event):
        editor = event.widget
        editor.focus_set()
        index = editor.index(f"@{event.x},{event.y}")
        selected = bool(editor.tag_ranges("sel"))
        # A right click inside the selection keeps it; anywhere else moves the caret there.
        if selected and not (
            editor.compare(index, ">=", "sel.first")
            and editor.compare(index, "<", "sel.last")
        ):
            editor.tag_remove("sel", "1.0", tk.END)
            editor.mark_set(tk.INSERT, index)
            selected = False
        elif not selected:
            editor.mark_set(tk.INSERT, index)
        menu = getattr(self, "editor_menu", None)
        if menu is None:
            menu = self.editor_menu = tk.Menu(self.root, tearoff=False)
            menu.add_command(label="Undo", command=self.undo)
            menu.add_command(label="Redo", command=self.redo)
            menu.add_separator()
            menu.add_command(label="Cut", command=self.cut)
            menu.add_command(label="Copy", command=self.copy)
            menu.add_command(label="Paste", command=self.paste)
            menu.add_separator()
            menu.add_command(label="Select All", command=self.select_all)
            menu.add_command(label="Find...", command=self.find_text)
        state = tk.NORMAL if selected else tk.DISABLED
        menu.entryconfigure("Cut", state=state)
        menu.entryconfigure("Copy", state=state)
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()
        return "break"

    def on_modified(self, doc):
        if doc not in self.docs:
            return
        if doc.editor.edit_modified():
            doc.editor.edit_modified(False)
            doc.dirty = True
            self._refresh_tab(doc)
            if doc is self.doc:
                self.update_line_numbers()
                self.update_title()
                doc.editor.tag_remove("error_line", "1.0", tk.END)
                self.schedule_highlight()
                self.update_status()
                if self.find_visible:
                    self.schedule_find_refresh()

    def on_key_release(self, event=None):
        self.update_status()
        self.highlight_matching_bracket()

    def highlight_matching_bracket(self):
        self.editor.tag_remove("bracket_match", "1.0", tk.END)
        try:
            cursor = self.editor.index(tk.INSERT)
            reverse = {value: key for key, value in self.BRACKET_PAIRS.items()}
            for position in (f"{cursor}-1c", cursor):
                char = self.editor.get(position, f"{position}+1c")
                if char in self.BRACKET_PAIRS:
                    opening, closing, direction = char, self.BRACKET_PAIRS[char], 1
                elif char in reverse:
                    opening, closing, direction = reverse[char], char, -1
                else:
                    continue
                match = self._find_matching_bracket(
                    position, opening, closing, direction
                )
                if match is not None:
                    self.editor.tag_add("bracket_match", position, f"{position}+1c")
                    self.editor.tag_add("bracket_match", match, f"{match}+1c")
                return
        except tk.TclError:
            return

    def _find_matching_bracket(self, position, opening, closing, direction):
        if direction > 0:
            start = self.editor.index(f"{position}+1c")
            text = self.editor.get(start, f"{start}+{self.BRACKET_SCAN_LIMIT}c")
            offsets = range(len(text))
            same = opening
        else:
            start = self.editor.index(f"{position}-{self.BRACKET_SCAN_LIMIT}c")
            text = self.editor.get(start, position)
            offsets = range(len(text) - 1, -1, -1)
            same = closing
        depth = 0
        for offset in offsets:
            char = text[offset]
            if char != opening and char != closing:
                continue
            at = f"{start}+{offset}c"
            tags = self.editor.tag_names(at)
            if "string" in tags or "pattern" in tags or "comment" in tags:
                continue
            if char == same:
                depth += 1
            elif depth == 0:
                return at
            else:
                depth -= 1
        return None

    def update_line_numbers(self):
        if not hasattr(self, "editor"):
            return
        line_count = int(self.editor.index("end-1c").split(".")[0])
        if line_count != self.numbered_lines:
            self.numbered_lines = line_count
            digits = max(4, len(str(line_count)) + 1)
            self.line_numbers.config(state=tk.NORMAL, width=digits)
            self.line_numbers.delete("1.0", tk.END)
            self.line_numbers.insert(
                "1.0", "\n".join(str(i) for i in range(1, line_count + 1))
            )
            self.line_numbers.config(state=tk.DISABLED)
        self.line_numbers.yview_moveto(self.editor.yview()[0])

    def _is_ident_start(self, char):
        return bool(char) and char in self.IDENTIFIER_START

    def _is_ident_part(self, char):
        return bool(char) and char in self.IDENTIFIER_PART

    def _scan_highlights(self, source):
        spans = []
        length = len(source)
        i = 0

        def add(start, end, tag):
            if tag and end > start:
                spans.append((start, end, tag))

        while i < length:
            start = i
            char = source[i]

            if source.startswith("note", i) and not self._is_ident_part(
                source[i - 1] if i else ""
            ):
                word_end = i + 4
                if word_end == length or not self._is_ident_part(source[word_end]):
                    if word_end < length and source[word_end] == ":":
                        comment_start = i
                        cursor = word_end + 1
                        while cursor < length and source[cursor] in " \t\r":
                            cursor += 1
                        if cursor >= length or source[cursor] == "\n":
                            if cursor < length:
                                cursor += 1
                            end_marker = None
                            search_at = cursor
                            while search_at < length:
                                found = source.find("endnote", search_at)
                                if found < 0:
                                    break
                                after = found + 7
                                if after >= length or not self._is_ident_part(
                                    source[after]
                                ):
                                    end_marker = after
                                    break
                                search_at = found + 7
                            i = end_marker if end_marker is not None else length
                        else:
                            newline = source.find("\n", cursor)
                            i = length if newline < 0 else newline
                        add(comment_start, i, "comment")
                        continue

            if (
                char == '"'
                or char == "'"
                or (char == "f" and i + 1 < length and source[i + 1] == '"')
            ):
                is_fstring = char == "f" and i + 1 < length and source[i + 1] == '"'
                quote_at = i + 1 if is_fstring else i
                quote = source[quote_at]
                cursor = quote_at + 1
                while cursor < length:
                    if source[cursor] == "\\":
                        cursor += 2
                        continue
                    if source[cursor] == quote:
                        cursor += 1
                        break
                    cursor += 1
                i = min(cursor, length)
                add(start, i, "string")
                continue

            if char == "p" and i + 1 < length and source[i + 1] == '"':
                cursor = i + 2
                while cursor < length:
                    if source[cursor] == "\\":
                        if cursor + 1 < length:
                            cursor += 2
                        else:
                            cursor += 1
                        continue
                    if source[cursor] == '"':
                        cursor += 1
                        break
                    cursor += 1
                while cursor < length and source[cursor] in "im":
                    cursor += 1
                i = cursor
                add(start, i, "pattern")
                continue

            if self._is_ident_start(char):
                cursor = i + 1
                while cursor < length and self._is_ident_part(source[cursor]):
                    cursor += 1
                word = source[i:cursor]
                if word in self.KEYWORDS:
                    tag = "keyword"
                elif word in self.TYPE_NAMES:
                    tag = "type"
                else:
                    next_cursor = cursor
                    while next_cursor < length and source[next_cursor] in " \t\r\n":
                        next_cursor += 1
                    if next_cursor < length and source[next_cursor] == "(":
                        tag = (
                            "builtin"
                            if word in self.BUILTINS or word in self.LIBRARY_FUNCTIONS
                            else "function"
                        )
                    else:
                        tag = None
                add(start, cursor, tag)
                i = cursor
                continue

            if char in "0123456789":
                cursor = i + 1
                while cursor < length and source[cursor] in "0123456789":
                    cursor += 1
                if (
                    cursor + 1 < length
                    and source[cursor] == "."
                    and source[cursor + 1] in "0123456789"
                ):
                    cursor += 1
                    while cursor < length and source[cursor] in "0123456789":
                        cursor += 1
                # An exponent: 1e6, 2.5E-3, 4e+2.
                if cursor < length and source[cursor] in "eE":
                    exp = cursor + 1
                    if exp < length and source[exp] in "+-":
                        exp += 1
                    if exp < length and source[exp] in "0123456789":
                        cursor = exp
                        while cursor < length and source[cursor] in "0123456789":
                            cursor += 1
                add(start, cursor, "number")
                i = cursor
                continue

            matched_operator = None
            for operator in self.OPERATORS:
                if source.startswith(operator, i):
                    matched_operator = operator
                    break
            if matched_operator:
                i += len(matched_operator)
                add(start, i, "operator")
                continue

            i += 1

        return spans

    def schedule_highlight(self):
        if self.highlight_job is not None:
            try:
                self.root.after_cancel(self.highlight_job)
            except tk.TclError:
                pass
        self.highlight_job = self.root.after(140, self.highlight_syntax)

    def highlight_syntax(self):
        self.highlight_job = None
        if not self.root.winfo_exists():
            return
        source = self.editor.get("1.0", "end-1c")
        tags = (
            "keyword",
            "builtin",
            "type",
            "string",
            "pattern",
            "comment",
            "number",
            "function",
            "operator",
        )
        for tag in tags:
            self.editor.tag_remove(tag, "1.0", tk.END)
        for start, end, tag in self._scan_highlights(source):
            self.editor.tag_add(tag, f"1.0+{start}c", f"1.0+{end}c")
        self.editor.tag_raise("comment")

    def update_status(self, event=None):
        if not hasattr(self, "editor"):
            return
        line, column = self.editor.index(tk.INSERT).split(".")
        self.position_var.set(f"Ln {line}, Col {int(column) + 1}")
        if self.running:
            self.status_var.set("Running")

    def update_title(self):
        doc = self.doc
        dirty = "* " if doc.dirty else ""
        self.root.title(f"{dirty}CuffScript IDE - {self._display_name(doc)}")
        self._refresh_tab(doc)

    def update_engine_status(self):
        if (
            self.engine_path
            and self.engine_path.is_file()
            and (os.name == "nt" or os.access(self.engine_path, os.X_OK))
        ):
            self.engine_var.set(f"Compiler: {self.engine_path.name}")
        else:
            self.engine_var.set("Compiler: Not found")

    def choose_engine(self):
        file_path = filedialog.askopenfilename(
            parent=self.root,
            title="Select CuffScript Compiler",
            filetypes=(
                [("Executable", "*.exe"), ("All files", "*")]
                if os.name == "nt"
                else [("All files", "*")]
            ),
        )
        if file_path:
            self.engine_path = Path(file_path).resolve()
            self.update_engine_status()

    def _display_name(self, doc):
        if doc.path is not None:
            return doc.path.name
        return (
            "Untitled"
            if doc.untitled_number <= 1
            else f"Untitled {doc.untitled_number}"
        )

    def _refresh_tab(self, doc):
        if doc.tab is None:
            return
        active = doc is self.doc
        background = self.TAB_ACTIVE_BG if active else self.TAB_BAR_BG
        foreground = (
            "#d4d4d4"
            if active and self.theme == "dark"
            else (
                "#222222"
                if active
                else "#a0a0a0" if self.theme == "dark" else "#6f6f6f"
            )
        )
        name = self._display_name(doc)
        if len(name) > 24:
            name = name[:23] + "…"
        try:
            for widget in (doc.tab, doc.tab_title, doc.tab_close):
                widget.configure(background=background)
            doc.tab_accent.configure(
                background=self.TAB_ACCENT if active else self.TAB_BAR_BG
            )
            doc.tab_title.configure(text=name, foreground=foreground)
            doc.tab_close.configure(
                text="●" if doc.dirty else "×", foreground=foreground
            )
        except tk.TclError:
            pass
        if hasattr(self, "tab_canvas"):
            self.update_tab_scroll_region()

    def _find_document(self, path):
        wanted = os.path.normcase(str(path))
        for doc in self.docs:
            if doc.path is not None and os.path.normcase(str(doc.path)) == wanted:
                return doc
        return None

    def select_document(self, doc):
        if doc is self.doc:
            doc.editor.focus_set()
            return
        if self.doc is not None:
            if self.highlight_job is not None:
                try:
                    self.root.after_cancel(self.highlight_job)
                except tk.TclError:
                    pass
                self.highlight_syntax()
            self.doc.frame.pack_forget()
        self.doc = doc
        doc.frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        for item in self.docs:
            self._refresh_tab(item)
        self.update_line_numbers()
        self.update_title()
        self.update_status()
        self.schedule_highlight()
        if doc.path is not None and self.folder is not None:
            self.select_in_tree(doc.path)
        if self.find_visible:
            self.refresh_find(jump=False)
        doc.editor.focus_set()

    def cycle_document(self, step):
        if len(self.docs) > 1:
            index = (self.docs.index(self.doc) + step) % len(self.docs)
            self.select_document(self.docs[index])
        return "break"

    def show_tab_menu(self, event=None):
        menu = tk.Menu(self.root, tearoff=False)
        for doc in self.docs:
            label = ("● " if doc.dirty else "") + self._display_name(doc)
            if doc is self.doc:
                label = "✓ " + label
            menu.add_command(label=label, command=lambda d=doc: self.select_document(d))
        x = self.tab_list_button.winfo_rootx()
        y = self.tab_list_button.winfo_rooty() + self.tab_list_button.winfo_height()
        try:
            menu.tk_popup(x, y)
        finally:
            menu.grab_release()
        return "break"

    def confirm_close(self, doc):
        if not doc.dirty:
            return True
        self.select_document(doc)
        answer = messagebox.askyesnocancel(
            "Unsaved Changes",
            f"'{self._display_name(doc)}' has unsaved changes.\nWould you like to save them?",
            parent=self.root,
        )
        if answer is None:
            return False
        if answer:
            return self.save_file()
        return True

    def close_current_document(self):
        return self.close_document(self.doc)

    def close_document(self, doc):
        if doc not in self.docs:
            return "break"
        if not self.confirm_close(doc):
            return "break"
        index = self.docs.index(doc)
        was_active = doc is self.doc
        self.docs.remove(doc)
        if self.last_run_doc is doc:
            self.last_run_doc = None
        if was_active:
            if self.highlight_job is not None:
                try:
                    self.root.after_cancel(self.highlight_job)
                except tk.TclError:
                    pass
                self.highlight_job = None
            self.doc = None
        doc.tab.destroy()
        doc.frame.destroy()
        if not self.docs:
            self.select_document(self._create_document())
        elif was_active:
            self.select_document(self.docs[min(index, len(self.docs) - 1)])
        return "break"

    def new_file(self):
        self.select_document(self._create_document())
        return "break"

    def open_file(self):
        paths = filedialog.askopenfilenames(
            parent=self.root,
            filetypes=[("CuffScript Files", "*.cuff"), ("All files", "*")],
        )
        if isinstance(paths, str):
            paths = self.root.tk.splitlist(paths)
        for file_path in paths:
            self.load_file(Path(file_path))
        return "break"

    def open_path(self, path):
        self.load_file(path)

    def load_file(self, path):
        try:
            resolved = path.resolve()
        except OSError:
            resolved = path
        existing = self._find_document(resolved)
        if existing is not None:
            self.select_document(existing)
            return
        try:
            if path.stat().st_size > 5_000_000:
                raise ValueError("File is too large (over 5 MB)")
            with open(path, "rb") as file:
                raw = file.read(5_000_001)
            has_bom = raw.startswith(codecs.BOM_UTF8)
            try:
                code = (
                    raw[len(codecs.BOM_UTF8) :].decode("utf-8")
                    if has_bom
                    else raw.decode("utf-8")
                )
            except UnicodeDecodeError:
                raise ValueError(
                    "The file is not valid UTF-8, so it was not opened to avoid damaging it."
                )
            stamp = self._disk_stamp(path)
        except Exception as exc:
            messagebox.showerror(
                "Open Error", f"Could not open the file.\n{exc}", parent=self.root
            )
            return
        current = self.doc
        reusable = (
            current.path is None
            and not current.dirty
            and not current.editor.get("1.0", "end-1c")
        )
        self.select_document(current if reusable else self._create_document())
        self.line_ending = "\r\n" if "\r\n" in code else "\n"
        self.doc.has_bom = has_bom
        self.doc.disk_stamp = stamp
        code = code.replace("\r\n", "\n")
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", code)
        self.editor.edit_reset()
        self.current_file_path = resolved
        self.editor.edit_modified(False)
        self.dirty = False
        self.update_line_numbers()
        self.update_title()
        self.schedule_highlight()
        self.editor.focus_set()
        if self.folder is None:
            self.set_folder(self.current_file_path.parent)
        self.select_in_tree(self.current_file_path)

    @staticmethod
    def _disk_stamp(path):
        try:
            info = os.stat(path)
            return (info.st_mtime_ns, info.st_size)
        except OSError:
            return None

    @staticmethod
    def _write_file(path, text, line_ending, bom):
        """Write via a temporary file and rename, so a crash or full disk cannot leave a
        half-written script. Symlinks are followed and file permissions are kept."""
        data = text.replace("\n", line_ending).encode("utf-8")
        if bom:
            data = codecs.BOM_UTF8 + data
        target = os.path.realpath(path)
        descriptor, temp_name = None, None
        try:
            descriptor, temp_name = tempfile.mkstemp(
                prefix=".cuffscript_save_", dir=os.path.dirname(target)
            )
            with os.fdopen(descriptor, "wb") as file:
                descriptor = None
                file.write(data)
                file.flush()
                os.fsync(file.fileno())
            try:
                shutil.copymode(target, temp_name)
            except OSError:
                pass
            os.replace(temp_name, target)
            temp_name = None
        except OSError:
            # Folder not writable, or the file is locked for replacement: fall back to in-place.
            with open(target, "wb") as file:
                file.write(data)
        finally:
            if descriptor is not None:
                try:
                    os.close(descriptor)
                except OSError:
                    pass
            if temp_name is not None:
                try:
                    os.unlink(temp_name)
                except OSError:
                    pass

    def save_file(self):
        doc = self.doc
        if doc.path is None:
            return self.save_as_file()
        current = self._disk_stamp(doc.path)
        if doc.disk_stamp is not None and current != doc.disk_stamp:
            if not messagebox.askyesno(
                "File Changed",
                f"{doc.path.name} was changed outside the IDE since it was opened.\n\n"
                "Overwrite it with the text in this tab?",
                icon="warning",
                parent=self.root,
            ):
                return False
        try:
            self._write_file(
                doc.path, doc.editor.get("1.0", "end-1c"), doc.line_ending, doc.has_bom
            )
            doc.disk_stamp = self._disk_stamp(doc.path)
            doc.editor.edit_modified(False)
            doc.dirty = False
            self.update_title()
            self.status_var.set("Saved")
            return True
        except Exception as exc:
            messagebox.showerror(
                "Save Error", f"Could not save the file.\n{exc}", parent=self.root
            )
            return False

    def copy_file_path(self):
        if self.current_file_path is None:
            self.status_var.set("Save the file before copying its path")
            return "break"
        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(str(self.current_file_path))
            self.status_var.set("Copied file path")
        except tk.TclError:
            self.status_var.set("Could not access the clipboard")
        return "break"

    def save_as_file(self):
        doc = self.doc
        file_path = filedialog.asksaveasfilename(
            parent=self.root,
            defaultextension=".cuff",
            filetypes=[("CuffScript Files", "*.cuff"), ("All files", "*")],
        )
        if not file_path:
            return False
        new_path = Path(file_path).resolve()
        other = self._find_document(new_path)
        if other is not None and other is not doc:
            messagebox.showerror(
                "Save Error",
                "This file is already open in another tab.\nClose that tab first.",
                parent=self.root,
            )
            return False
        if new_path.exists() and new_path != doc.path and new_path.is_dir():
            messagebox.showerror(
                "Save Error", "That path is a folder.", parent=self.root
            )
            return False
        try:
            self._write_file(
                new_path, doc.editor.get("1.0", "end-1c"), doc.line_ending, doc.has_bom
            )
            doc.disk_stamp = self._disk_stamp(new_path)
        except Exception as exc:
            messagebox.showerror(
                "Save Error", f"Could not save the file.\n{exc}", parent=self.root
            )
            return False
        doc.path = new_path
        doc.editor.edit_modified(False)
        doc.dirty = False
        if self.folder is None:
            self.set_folder(new_path.parent)
        else:
            self.refresh_tree()
        self.select_in_tree(new_path)
        self.update_title()
        self.status_var.set("Saved")
        return True

    def insert_indent(self, event=None):
        try:
            self.editor.index("sel.first")
            return self.indent_selection()
        except tk.TclError:
            self.editor.insert(tk.INSERT, "    ")
            return "break"

    def indent_selection(self):
        try:
            start_line = int(self.editor.index("sel.first").split(".")[0])
            end_index = self.editor.index("sel.last")
            end_line = int(end_index.split(".")[0])
            if end_index.endswith(".0") and end_line > start_line:
                end_line -= 1
        except tk.TclError:
            self.editor.insert(tk.INSERT, "    ")
            return "break"
        self.editor.edit_separator()
        for line in range(start_line, end_line + 1):
            self.editor.insert(f"{line}.0", "    ")
        self.editor.edit_separator()
        self.editor.tag_remove("sel", "1.0", tk.END)
        self.editor.tag_add("sel", f"{start_line}.0", f"{end_line}.end")
        return "break"

    def remove_indent(self, event=None):
        try:
            start_line = int(self.editor.index("sel.first").split(".")[0])
            end_index = self.editor.index("sel.last")
            end_line = int(end_index.split(".")[0])
            if end_index.endswith(".0") and end_line > start_line:
                end_line -= 1
        except tk.TclError:
            start_line = end_line = int(self.editor.index(tk.INSERT).split(".")[0])
        self.editor.edit_separator()
        for line in range(start_line, end_line + 1):
            prefix = self.editor.get(f"{line}.0", f"{line}.0+4c")
            if prefix.startswith("\t"):
                self.editor.delete(f"{line}.0", f"{line}.0+1c")
            else:
                spaces = len(prefix) - len(prefix.lstrip(" "))
                remove_count = min(4, spaces)
                if remove_count:
                    self.editor.delete(f"{line}.0", f"{line}.0+{remove_count}c")
        self.editor.edit_separator()
        return "break"

    def insert_newline(self, event=None):
        try:
            line_start = self.editor.index("insert linestart")
            current_line = self.editor.get(line_start, tk.INSERT)
            full_line = self.editor.get(line_start, f"{line_start} lineend")
            indent = re.match(r"[ \t]*", current_line).group(0)
            if self.editor.tag_ranges("sel"):
                self.editor.delete("sel.first", "sel.last")
            stripped = current_line.rstrip()
            if re.search(r"\bdo:\s*$", stripped) and not re.search(
                r"\bend\s*$", full_line.strip()
            ):
                indent += "    "
            self.editor.insert(tk.INSERT, "\n" + indent)
            return "break"
        except tk.TclError:
            return None

    def smart_backspace(self, event=None):
        line_start = self.editor.index("insert linestart")
        before_cursor = self.editor.get(line_start, tk.INSERT)
        if before_cursor and not before_cursor.strip():
            if before_cursor.endswith("\t"):
                self.editor.delete("insert-1c", tk.INSERT)
                return "break"
            trailing_spaces = len(before_cursor) - len(before_cursor.rstrip(" "))
            if trailing_spaces:
                remove_count = min(4, trailing_spaces)
                self.editor.delete(f"insert-{remove_count}c", tk.INSERT)
                return "break"
        return None

    def undo(self):
        try:
            self.editor.edit_undo()
        except tk.TclError:
            pass
        return "break"

    def redo(self):
        try:
            self.editor.edit_redo()
        except tk.TclError:
            pass
        return "break"

    def _build_find_bar(self, parent):
        background = self.FIND_BAR_BG
        self.find_var = tk.StringVar()
        self.replace_var = tk.StringVar()
        self.find_case = tk.BooleanVar(value=False)
        self.find_word = tk.BooleanVar(value=False)
        self.find_regex = tk.BooleanVar(value=False)
        self.find_count_var = tk.StringVar()

        # Labels have no theme-aware default colour: set it so the recolouring in apply_theme
        # (which maps #24292f to the dark foreground) also reaches them.
        label_color = (
            "#d4d4d4" if getattr(self, "theme", "light") == "dark" else "#24292f"
        )
        self.find_bar = tk.Frame(parent, background=background)
        tk.Frame(self.find_bar, height=1, background="#d8d8d8").pack(
            side=tk.BOTTOM, fill=tk.X
        )
        top = tk.Frame(self.find_bar, background=background)
        top.pack(side=tk.TOP, fill=tk.X, padx=8, pady=(6, 5))
        tk.Label(
            top,
            text="Find",
            width=6,
            anchor="w",
            background=background,
            foreground=label_color,
        ).pack(side=tk.LEFT)
        self.find_entry = self._make_find_entry(top, self.find_var)
        self.find_entry.pack(side=tk.LEFT, ipady=2)
        tk.Label(
            top,
            textvariable=self.find_count_var,
            width=9,
            anchor="w",
            background=background,
            foreground="#6f6f6f",
        ).pack(side=tk.LEFT, padx=(8, 4))
        for text, variable in (
            ("Aa", self.find_case),
            ("Word", self.find_word),
            (".*", self.find_regex),
        ):
            ttk.Checkbutton(
                top,
                text=text,
                variable=variable,
                style="Toolbutton",
                command=self.refresh_find,
            ).pack(side=tk.LEFT, padx=1)
        ttk.Button(
            top, text="Previous", width=8, command=lambda: self.find_step(-1)
        ).pack(side=tk.LEFT, padx=(8, 2))
        ttk.Button(top, text="Next", width=6, command=lambda: self.find_step(1)).pack(
            side=tk.LEFT
        )
        close = tk.Label(
            top,
            text="✕",
            background=background,
            foreground="#6f6f6f",
            padx=6,
            cursor="hand2",
        )
        close.pack(side=tk.RIGHT)
        close.bind("<Button-1>", lambda event: self.close_find_bar())

        self.replace_row = tk.Frame(self.find_bar, background=background)
        tk.Label(
            self.replace_row,
            text="Replace",
            width=6,
            anchor="w",
            background=background,
            foreground=label_color,
        ).pack(side=tk.LEFT)
        self.replace_entry = self._make_find_entry(self.replace_row, self.replace_var)
        self.replace_entry.pack(side=tk.LEFT, ipady=2)
        ttk.Button(self.replace_row, text="Replace", command=self.replace_current).pack(
            side=tk.LEFT, padx=(8, 2)
        )
        ttk.Button(self.replace_row, text="Replace All", command=self.replace_all).pack(
            side=tk.LEFT
        )

        self.find_var.trace_add("write", lambda *args: self.refresh_find())
        modifier = "Command" if sys.platform == "darwin" else "Control"
        for entry in (self.find_entry, self.replace_entry):
            entry.bind("<Escape>", lambda event: self.close_find_bar())
            entry.bind(f"<{modifier}-f>", lambda event: self.open_find_bar(False))
            entry.bind(f"<{modifier}-h>", lambda event: self.open_find_bar(True))
            entry.bind("<F3>", lambda event: self.find_step(1))
            entry.bind("<Shift-F3>", lambda event: self.find_step(-1))
            if sys.platform != "darwin":
                entry.bind(
                    "<Control-a>", lambda event: self._select_entry_text(event.widget)
                )
        self.find_entry.bind("<Return>", lambda event: self.find_step(1))
        self.find_entry.bind("<KP_Enter>", lambda event: self.find_step(1))
        self.find_entry.bind("<Shift-Return>", lambda event: self.find_step(-1))
        self.replace_entry.bind("<Return>", lambda event: self.replace_current())
        self.replace_entry.bind("<KP_Enter>", lambda event: self.replace_current())

    def _make_find_entry(self, parent, variable):
        return tk.Entry(
            parent,
            textvariable=variable,
            width=30,
            relief=tk.FLAT,
            background="#ffffff",
            foreground="#24292f",
            insertbackground="#24292f",
            highlightthickness=1,
            highlightbackground="#c8c8c8",
            highlightcolor=self.TAB_ACCENT,
        )

    @staticmethod
    def _select_entry_text(entry):
        entry.select_range(0, tk.END)
        entry.icursor(tk.END)
        return "break"

    def _find_pattern(self):
        query = self.find_var.get()
        if not query:
            return None, ""
        body = query if self.find_regex.get() else re.escape(query)
        if self.find_word.get():
            body = r"(?<!\w)(?:" + body + r")(?!\w)"
        flags = re.MULTILINE | (0 if self.find_case.get() else re.IGNORECASE)
        try:
            return re.compile(body, flags), ""
        except re.error as exc:
            return None, str(exc)

    @staticmethod
    def _collect_matches(pattern, text, limit=None):
        found = []
        for match in pattern.finditer(text):
            if match.end() > match.start():
                found.append(match)
                if limit is not None and len(found) >= limit:
                    break
        return found

    def _text_offset(self, index):
        return len(self.editor.get("1.0", index))

    def _selection_offsets(self):
        try:
            return self._text_offset("sel.first"), self._text_offset("sel.last")
        except tk.TclError:
            return None

    def _update_find_count(self, current=None):
        total = f"{len(self.find_matches)}{'+' if self.find_truncated else ''}"
        self.find_count_var.set(
            f"{current + 1}/{total}" if current is not None else f"{total} matches"
        )

    def schedule_find_refresh(self):
        if self.find_job is not None:
            try:
                self.root.after_cancel(self.find_job)
            except tk.TclError:
                pass
        self.find_job = self.root.after(200, lambda: self.refresh_find(jump=False))

    def refresh_find(self, jump=True):
        if self.find_job is not None:
            try:
                self.root.after_cancel(self.find_job)
            except tk.TclError:
                pass
            self.find_job = None
        if not self.find_visible or self.doc is None:
            return
        editor = self.editor
        editor.tag_remove("search_match", "1.0", tk.END)
        editor.tag_remove("search_current", "1.0", tk.END)
        self.find_matches = []
        self.find_truncated = False
        pattern, error = self._find_pattern()
        if pattern is None:
            self.find_count_var.set("Invalid regex" if error else "")
            self.find_entry.configure(
                background=self.FIND_ERROR_BG if error else "#ffffff"
            )
            return
        text = editor.get("1.0", "end-1c")
        found = self._collect_matches(pattern, text, self.MAX_FIND_MATCHES + 1)
        self.find_truncated = len(found) > self.MAX_FIND_MATCHES
        self.find_matches = [
            (match.start(), match.end()) for match in found[: self.MAX_FIND_MATCHES]
        ]
        for start, end in self.find_matches:
            editor.tag_add("search_match", f"1.0+{start}c", f"1.0+{end}c")
        if not self.find_matches:
            self.find_count_var.set("No results")
            self.find_entry.configure(background=self.FIND_ERROR_BG)
            return
        self.find_entry.configure(background="#ffffff")
        if jump:
            selection = self._selection_offsets()
            reference = selection[0] if selection else self._text_offset(tk.INSERT)
            index = next(
                (
                    i
                    for i, (start, _end) in enumerate(self.find_matches)
                    if start >= reference
                ),
                0,
            )
            self._select_match(index)
        else:
            self._update_find_count()

    def _select_match(self, index):
        start, end = self.find_matches[index]
        editor = self.editor
        first, last = f"1.0+{start}c", f"1.0+{end}c"
        editor.tag_remove("search_current", "1.0", tk.END)
        editor.tag_add("search_current", first, last)
        editor.tag_remove("sel", "1.0", tk.END)
        editor.tag_add("sel", first, last)
        editor.mark_set(tk.INSERT, last)
        editor.see(first)
        self._update_find_count(index)
        self.update_status()

    def find_step(self, direction):
        if not self.find_visible:
            self.open_find_bar(False)
            return "break"
        self.refresh_find(jump=False)
        matches = self.find_matches
        if not matches:
            self.status_var.set("No search results")
            return "break"
        selection = self._selection_offsets()
        wrapped = False
        if direction >= 0:
            reference = selection[1] if selection else self._text_offset(tk.INSERT)
            index = next(
                (i for i, (start, _end) in enumerate(matches) if start >= reference),
                None,
            )
            if index is None:
                index, wrapped = 0, True
        else:
            reference = selection[0] if selection else self._text_offset(tk.INSERT)
            index = next(
                (
                    i
                    for i in range(len(matches) - 1, -1, -1)
                    if matches[i][1] <= reference
                ),
                None,
            )
            if index is None:
                index, wrapped = len(matches) - 1, True
        self._select_match(index)
        if wrapped:
            self.status_var.set(
                "Wrapped to the beginning" if direction >= 0 else "Wrapped to the end"
            )
        return "break"

    def _expand_replacement(self, match):
        replacement = self.replace_var.get()
        if not self.find_regex.get():
            return replacement
        try:
            return match.expand(replacement)
        except (re.error, IndexError) as exc:
            raise ValueError(f"Invalid replacement: {exc}")

    def _edit_as_one_step(self, action):
        editor = self.editor
        editor.configure(autoseparators=False)
        editor.edit_separator()
        try:
            action()
        finally:
            editor.edit_separator()
            editor.configure(autoseparators=True)

    def replace_current(self):
        pattern, _error = self._find_pattern()
        if pattern is None:
            return "break"
        editor = self.editor
        selection = self._selection_offsets()
        if selection:
            match = pattern.match(editor.get("1.0", "end-1c"), selection[0])
            if (
                match
                and (match.start(), match.end()) == selection
                and match.end() > match.start()
            ):
                try:
                    replacement = self._expand_replacement(match)
                except ValueError as exc:
                    self.status_var.set(str(exc))
                    return "break"
                first = f"1.0+{match.start()}c"

                def apply():
                    editor.delete(first, f"1.0+{match.end()}c")
                    editor.insert(first, replacement)

                self._edit_as_one_step(apply)
                editor.tag_remove("sel", "1.0", tk.END)
                editor.mark_set(tk.INSERT, f"{first}+{len(replacement)}c")
        return self.find_step(1)

    def replace_all(self):
        pattern, _error = self._find_pattern()
        if pattern is None:
            return "break"
        editor = self.editor
        found = self._collect_matches(pattern, editor.get("1.0", "end-1c"))
        if not found:
            self.status_var.set("Nothing to replace")
            return "break"
        try:
            replacements = [self._expand_replacement(match) for match in found]
        except ValueError as exc:
            self.status_var.set(str(exc))
            return "break"

        def apply():
            for match, replacement in zip(reversed(found), reversed(replacements)):
                first = f"1.0+{match.start()}c"
                editor.delete(first, f"1.0+{match.end()}c")
                editor.insert(first, replacement)

        self._edit_as_one_step(apply)
        editor.tag_remove("sel", "1.0", tk.END)
        self.refresh_find(jump=False)
        self.status_var.set(f"Replaced {len(found)} matches")
        return "break"

    def open_find_bar(self, replace=False):
        editor = self.editor
        selection = editor.tag_ranges("sel")
        current = editor.tag_ranges("search_current")
        is_find_result = (
            bool(selection)
            and bool(current)
            and (
                (str(selection[0]), str(selection[1]))
                == (str(current[0]), str(current[1]))
            )
        )
        if selection and not is_find_result:
            selected = editor.get("sel.first", "sel.last")
            if selected and "\n" not in selected and len(selected) <= 200:
                self.find_var.set(
                    re.escape(selected) if self.find_regex.get() else selected
                )
        if not self.find_visible:
            self.find_bar.pack(side=tk.TOP, fill=tk.X, before=self.doc_container)
            self.find_visible = True
        if replace and not self.replace_visible:
            self.replace_row.pack(side=tk.TOP, fill=tk.X, padx=8, pady=(0, 5))
            self.replace_visible = True
        self.find_entry.focus_set()
        self._select_entry_text(self.find_entry)
        self.refresh_find(jump=True)
        return "break"

    def close_find_bar(self):
        if not self.find_visible:
            return None
        self.find_bar.pack_forget()
        self.find_visible = False
        if self.find_job is not None:
            try:
                self.root.after_cancel(self.find_job)
            except tk.TclError:
                pass
            self.find_job = None
        for doc in self.docs:
            doc.editor.tag_remove("search_match", "1.0", tk.END)
            doc.editor.tag_remove("search_current", "1.0", tk.END)
        self.find_matches = []
        self.editor.focus_set()
        return "break"

    def find_text(self):
        return self.open_find_bar(False)

    def find_next(self, event=None, from_start=False):
        return self.find_step(1)

    def find_previous(self, event=None):
        return self.find_step(-1)

    def cut(self):
        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(self.editor.get("sel.first", "sel.last"))
            self.editor.edit_separator()
            self.editor.delete("sel.first", "sel.last")
            self.editor.edit_separator()
        except tk.TclError:
            pass
        self.editor.focus_set()
        return "break"

    def copy(self):
        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(self.editor.get("sel.first", "sel.last"))
        except tk.TclError:
            pass
        return "break"

    def paste(self):
        try:
            text = self.root.clipboard_get()
            self.editor.edit_separator()
            if self.editor.tag_ranges("sel"):
                self.editor.delete("sel.first", "sel.last")
            self.editor.insert(tk.INSERT, text)
            self.editor.edit_separator()
        except tk.TclError:
            pass
        self.editor.focus_set()
        return "break"

    def select_all(self, event=None):
        self.editor.tag_add("sel", "1.0", "end-1c")
        self.editor.mark_set(tk.INSERT, "1.0")
        self.editor.see(tk.INSERT)
        self.editor.focus_set()
        return "break"

    def replace_text(self, event=None):
        return self.open_find_bar(True)

    def go_to_line(self, event=None):
        last_line = int(self.editor.index("end-1c").split(".")[0])
        line = simpledialog.askinteger(
            "Go to Line",
            f"Line number (1-{last_line}):",
            minvalue=1,
            maxvalue=last_line,
            parent=self.root,
        )
        if line is not None:
            self.editor.mark_set(tk.INSERT, f"{line}.0")
            self.editor.see(tk.INSERT)
            self.editor.focus_set()
            self.update_status()
        return "break"

    def duplicate_line(self, event=None):
        line = int(self.editor.index(tk.INSERT).split(".")[0])
        content = self.editor.get(f"{line}.0", f"{line}.end")
        self.editor.edit_separator()
        self.editor.insert(f"{line}.end", "\n" + content)
        self.editor.edit_separator()
        self.editor.mark_set(tk.INSERT, f"{line + 1}.end")
        self.editor.focus_set()
        return "break"

    def delete_line(self, event=None):
        line = int(self.editor.index(tk.INSERT).split(".")[0])
        last_line = int(self.editor.index("end-1c").split(".")[0])
        self.editor.edit_separator()
        if last_line == 1:
            self.editor.delete("1.0", "end-1c")
        elif line < last_line:
            self.editor.delete(f"{line}.0", f"{line + 1}.0")
        else:
            self.editor.delete(f"{line - 1}.end", f"{line}.end")
            self.editor.mark_set(tk.INSERT, f"{line - 1}.end")
        self.editor.edit_separator()
        self.editor.focus_set()
        return "break"

    COMMENT_PREFIX = "note: "

    def _selected_lines(self):
        try:
            start_line = int(self.editor.index("sel.first").split(".")[0])
            end_index = self.editor.index("sel.last")
            end_line = int(end_index.split(".")[0])
            if end_index.endswith(".0") and end_line > start_line:
                end_line -= 1
        except tk.TclError:
            start_line = end_line = int(self.editor.index(tk.INSERT).split(".")[0])
        return start_line, end_line

    def toggle_comment(self, event=None):
        start_line, end_line = self._selected_lines()
        lines = [
            self.editor.get(f"{n}.0", f"{n}.end")
            for n in range(start_line, end_line + 1)
        ]
        if (
            len(lines) >= 2
            and lines[0].strip() == "note:"
            and lines[-1].strip() == "endnote"
        ):
            return self.uncomment_selection()
        content = [text for text in lines if text.strip()]
        commented = bool(content) and all(
            text.lstrip(" \t").startswith(self.COMMENT_PREFIX) for text in content
        )
        self.editor.edit_separator()
        for number, text in zip(range(start_line, end_line + 1), lines):
            if not text.strip():
                continue
            indent = len(text) - len(text.lstrip(" \t"))
            if commented:
                self.editor.delete(
                    f"{number}.{indent}",
                    f"{number}.{indent + len(self.COMMENT_PREFIX)}",
                )
            else:
                self.editor.insert(f"{number}.{indent}", self.COMMENT_PREFIX)
        self.editor.edit_separator()
        self.editor.focus_set()
        return "break"

    def comment_selection(self):
        return self.toggle_comment()

    def uncomment_selection(self):
        try:
            start_line = int(self.editor.index("sel.first").split(".")[0])
            end_index = self.editor.index("sel.last")
            end_line = int(end_index.split(".")[0])
            if end_index.endswith(".0") and end_line > start_line:
                end_line -= 1
        except tk.TclError:
            start_line = end_line = int(self.editor.index(tk.INSERT).split(".")[0])
        start = f"{start_line}.0"
        end = f"{end_line}.end"
        content = self.editor.get(start, end)
        lines = content.splitlines()
        if (
            len(lines) >= 2
            and lines[0].strip() == "note:"
            and lines[-1].strip() == "endnote"
        ):
            replacement = "\n".join(lines[1:-1])
            self.editor.edit_separator()
            self.editor.delete(start, end)
            self.editor.insert(start, replacement)
            self.editor.edit_separator()
        self.editor.focus_set()
        return "break"

    def autocomplete(self, event=None):
        line, column = self.editor.index(tk.INSERT).split(".")
        before = self.editor.get(f"{line}.0", tk.INSERT)
        match = re.search(r"[A-Za-z_][A-Za-z_0-9]*$", before)
        if not match:
            return "break"
        prefix = match.group(0)
        words = self.KEYWORDS | self.TYPE_NAMES | self.BUILTINS | self.LIBRARY_FUNCTIONS
        candidates = sorted(
            word for word in words if word.startswith(prefix) and word != prefix
        )
        if not candidates:
            self.status_var.set("No autocomplete suggestions")
            return "break"
        menu = tk.Menu(self.root, tearoff=False)
        self.completion_menu = menu
        for candidate in candidates[:30]:
            menu.add_command(
                label=candidate,
                command=lambda value=candidate, start=match.start(): self._insert_completion(
                    value, start
                ),
            )
        x = self.editor.winfo_rootx() + self.editor.winfo_width() // 3
        y = self.editor.winfo_rooty() + self.editor.winfo_height() // 3
        try:
            bbox = self.editor.bbox(tk.INSERT)
            if bbox:
                x = self.editor.winfo_rootx() + bbox[0]
                y = self.editor.winfo_rooty() + bbox[1] + bbox[3]
        except tk.TclError:
            pass
        menu.tk_popup(x, y)
        return "break"

    def _insert_completion(self, value, start):
        line = self.editor.index(tk.INSERT).split(".")[0]
        self.editor.delete(f"{line}.{start}", tk.INSERT)
        self.editor.insert(tk.INSERT, value)
        self.editor.focus_set()

    ABOUT_LINKS = (
        ("GitHub", "https://github.com/cuffscript/cuffscript/"),
        ("Website", "https://cuffscript.pages.dev/"),
    )

    def open_link(self, url):
        try:
            opened = webbrowser.open(url)
        except Exception:
            opened = False
        if not opened:
            # No browser available: put the address on the clipboard instead.
            try:
                self.root.clipboard_clear()
                self.root.clipboard_append(url)
                self.status_var.set(f"Could not open a browser; copied {url}")
            except tk.TclError:
                pass

    def show_about(self):
        existing = getattr(self, "about_dialog", None)
        try:
            if existing is not None and existing.winfo_exists():
                existing.lift()
                existing.focus_set()
                return
        except tk.TclError:
            pass
        try:
            tk_version = self.root.tk.call("info", "patchlevel")
        except tk.TclError:
            tk_version = "Unavailable"
        python_version = ".".join(str(part) for part in sys.version_info[:3])
        dark = self.theme == "dark"
        background = "#252526" if dark else "#ffffff"
        foreground = "#f1f1f1" if dark else "#24292f"
        muted = "#a0a0a0" if dark else "#6f6f6f"
        dialog = tk.Toplevel(self.root)
        dialog.title("About CuffScript IDE")
        dialog.transient(self.root)
        dialog.resizable(False, False)
        dialog.configure(background=background)
        content = tk.Frame(dialog, background=background, padx=24, pady=20)
        content.pack(fill=tk.BOTH, expand=True)
        heading = tk.Frame(content, background=background)
        heading.pack(fill=tk.X)
        titles = tk.Frame(heading, background=background)
        titles.pack(side=tk.LEFT, fill=tk.X, expand=True)
        tk.Label(
            titles,
            text="CuffScript IDE",
            font=(self.font_family, 16, "bold"),
            background=background,
            foreground=foreground,
        ).pack(anchor="w")
        tk.Label(
            titles,
            text="A lightweight editor for CuffScript",
            background=background,
            foreground=muted,
        ).pack(anchor="w", pady=(3, 0))
        tk.Frame(content, height=1, background="#3e3e42" if dark else "#e5e5e5").pack(
            fill=tk.X, pady=16
        )
        tk.Label(
            content,
            text=f"Python {python_version}    Tcl/Tk {tk_version}\nRequires Python 3.8 or later",
            justify=tk.LEFT,
            background=background,
            foreground=muted,
        ).pack(anchor="w")
        link_color = "#4daafc" if dark else "#0969da"
        for label, url in self.ABOUT_LINKS:
            row = tk.Frame(content, background=background)
            row.pack(anchor="w", pady=(10, 0))
            tk.Label(
                row,
                text=f"{label}:",
                width=8,
                anchor="w",
                background=background,
                foreground=muted,
            ).pack(side=tk.LEFT)
            link = tk.Label(
                row,
                text=url,
                cursor="hand2",
                background=background,
                foreground=link_color,
                font=tkfont.Font(
                    family=self.font_family,
                    size=max(9, self.font_size - 2),
                    underline=True,
                ),
            )
            link.pack(side=tk.LEFT)
            link.bind("<Button-1>", lambda event, u=url: self.open_link(u))
        ttk.Button(content, text="Close", command=dialog.destroy).pack(
            anchor="e", pady=(16, 0)
        )
        dialog.update_idletasks()
        dialog.geometry(
            f"{max(420, dialog.winfo_reqwidth())}x{dialog.winfo_reqheight()}"
        )
        self.about_dialog = dialog
        dialog.bind("<Escape>", lambda event: dialog.destroy())
        dialog.bind("<Return>", lambda event: dialog.destroy())
        dialog.grab_set()
        dialog.focus_set()

    def _tab_width(self):
        return (
            tkfont.Font(
                root=self.root, family=self.font_family, size=self.font_size
            ).measure("0")
            * 4
        )

    def _apply_font(self):
        font = (self.font_family, self.font_size)
        tabs = (self._tab_width(),)
        for doc in self.docs:
            doc.editor.configure(font=font, tabs=tabs)
            doc.line_numbers.configure(font=font)
            doc.editor.tag_configure("comment", font=self._comment_font())

    def change_font_size(self, amount):
        self.font_size = max(8, min(28, self.font_size + amount))
        self._apply_font()

    def reset_font_size(self):
        self.font_size = 12
        self._apply_font()

    def run_code(self, event=None):
        if self.running:
            self.status_var.set("A script is already running")
            return "break"
        if self.doc is None:
            return "break"
        if not self.command_input_active:
            self._prepare_command_input()
        # Echo the same full command a user would type (%RUN <file>), but run the tab's current
        # text, so unsaved edits are included.
        shown = self._quote_argument(self._display_name(self.doc))
        replayable = (
            self.doc.path is not None and self.doc.path.parent == self._terminal_cwd()
        )
        self.command_override = (lambda: self.cmd_run([]), replayable)
        self.output.delete("terminal_input_start", "end-1c")
        self.output.insert("end-1c", f"%RUN {shown}")
        self.output.mark_set(tk.INSERT, "end-1c")
        return self.submit_terminal_input()

    def _fail_run(self, text, clear):
        if clear:
            self._set_output(text + "\n", "error")
        else:
            self._terminal_write(text + "\n", "error")
        self.status_var.set("Run Error")
        self._prepare_command_input()

    def _terminal_cwd(self):
        return Path(self.shell_cwd or self.folder or self.base_dir)

    @staticmethod
    def _quote_argument(value):
        return (
            f'"{value}"'
            if (not value or any(char.isspace() for char in value))
            else value
        )

    def start_run(self, path=None, flags=(), clear=True):
        if self.running:
            self.status_var.set("A script is already running")
            return
        if not self.engine_path.is_file() or (
            os.name != "nt" and not os.access(self.engine_path, os.X_OK)
        ):
            engine_name = "cuffc.exe" if os.name == "nt" else "cuffc"
            self._fail_run(
                f"CuffScript compiler not found.\nChecked path: {self.engine_path}\n\n"
                f"Place {engine_name} beside the IDE, or select it from Settings > Select Compiler.",
                clear,
            )
            return

        all_flags = [*self.run_flags, *flags]
        target_doc = None
        target_path = None
        if path is None:
            target_doc = self.doc
        else:
            open_doc = self._find_document(path)
            if open_doc is not None and open_doc.dirty:
                target_doc = open_doc
            else:
                target_path = path

        temp_path = None
        if target_doc is not None:
            source = target_doc.editor.get("1.0", "end-1c")
            run_dir = (
                target_doc.path.parent if target_doc.path else self._terminal_cwd()
            )
            file_descriptor = None
            try:
                file_descriptor, temp_name = tempfile.mkstemp(
                    prefix=".cuffscript_ide_", suffix=".cuff", dir=str(run_dir)
                )
                temp_path = Path(temp_name)
                with os.fdopen(
                    file_descriptor, "w", encoding="utf-8", newline="\n"
                ) as temp_file:
                    file_descriptor = None
                    temp_file.write(source)
            except Exception as exc:
                if file_descriptor is not None:
                    try:
                        os.close(file_descriptor)
                    except OSError:
                        pass
                if temp_path is not None:
                    try:
                        temp_path.unlink(missing_ok=True)
                    except OSError:
                        pass
                self.temp_path = None
                self.temp_arg = None
                self._fail_run(f"Could not create a temporary file.\n{exc}", clear)
                return
            self.temp_path = temp_path
            self.temp_arg = temp_path.name
            argument = temp_path.name
            self.last_run_doc = target_doc
            self.last_run_source = source
            self.last_run_path = None
        else:
            # Keep the argument ASCII on Windows because the engine receives argv in the ANSI code page.
            run_dir = target_path.parent
            argument = target_path.name
            if os.name == "nt" and not argument.isascii():
                file_descriptor = None
                try:
                    file_descriptor, temp_name = tempfile.mkstemp(
                        prefix=".cuffscript_ide_", suffix=".cuff", dir=str(run_dir)
                    )
                    os.close(file_descriptor)
                    file_descriptor = None
                    temp_path = Path(temp_name)
                    shutil.copyfile(target_path, temp_path)
                    argument = temp_path.name
                except Exception as exc:
                    if file_descriptor is not None:
                        try:
                            os.close(file_descriptor)
                        except OSError:
                            pass
                    if temp_path is not None:
                        try:
                            temp_path.unlink(missing_ok=True)
                        except OSError:
                            pass
                    self.temp_path = None
                    self.temp_arg = None
                    self._fail_run(
                        f"Could not prepare the script for execution.\n{exc}", clear
                    )
                    return
                self.temp_path = temp_path
                self.temp_arg = temp_path.name
            else:
                self.temp_path = None
                self.temp_arg = None
            self.last_run_doc = self._find_document(target_path)
            self.last_run_source = (
                self.last_run_doc.editor.get("1.0", "end-1c")
                if self.last_run_doc is not None
                else None
            )
            self.last_run_path = target_path

        while True:
            try:
                self.result_queue.get_nowait()
            except queue.Empty:
                break

        self.pending_input_lines.clear()
        self.stdin_queue = queue.Queue()
        self.process = None
        self.worker_thread = None
        self.last_run_dir = run_dir
        self.run_dir = run_dir
        self.ansi_color = None
        self.ansi_tail = ""
        self.carriage_return = False
        self.history_index = len(self.input_history)
        self.history_draft = ""
        if clear:
            self._set_output("")
        self.command_input_active = False
        self.running = True
        self.set_terminal_cursor(True)
        self.run_started = time.monotonic()
        self.stop_event = threading.Event()
        self.output.config(state=tk.NORMAL)
        self.output.mark_set("terminal_input_start", "end-1c")
        self.output.mark_gravity("terminal_input_start", "left")
        self.output.mark_set(tk.INSERT, "end-1c")
        self.status_var.set("Running")
        self.output.focus_set()

        worker = threading.Thread(
            target=self._run_worker,
            args=(argument, temp_path, run_dir, self.stop_event, all_flags),
            daemon=True,
        )
        self.worker_thread = worker
        try:
            worker.start()
        except Exception as exc:
            self.running = False
            self.worker_thread = None
            if temp_path is not None:
                try:
                    temp_path.unlink(missing_ok=True)
                except OSError:
                    pass
            self._terminal_write(
                f"Could not start the worker thread.\n{exc}\n", "error"
            )
            self.status_var.set("Run Error")
            self._prepare_command_input()
            return
        self.poll_job = self.root.after(40, self._poll_run_result)

    def _run_worker(self, argument, cleanup_path, run_dir, stop_event, flags):
        process = None
        status = "finished"
        exit_code = None
        try:
            creation_options = {}
            if os.name == "nt":
                creation_options["creationflags"] = getattr(
                    subprocess, "CREATE_NEW_PROCESS_GROUP", 0
                ) | getattr(subprocess, "CREATE_NO_WINDOW", 0)
            else:
                creation_options["start_new_session"] = True
            process = subprocess.Popen(
                [str(self.engine_path), *flags, argument],
                cwd=str(run_dir),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0,
                **creation_options,
            )
            self.process = process

            def write_input():
                try:
                    while True:
                        value = self.stdin_queue.get()
                        if value is None:
                            break
                        data = memoryview((value + "\n").encode("utf-8"))
                        while data:
                            written = process.stdin.write(data)
                            if not written:
                                raise BrokenPipeError("Input pipe is closed")
                            data = data[written:]
                except (BrokenPipeError, OSError, ValueError) as exc:
                    self.result_queue.put(("input_error", str(exc)))
                finally:
                    try:
                        if process.stdin:
                            process.stdin.close()
                    except Exception:
                        pass

            input_thread = threading.Thread(target=write_input, daemon=True)
            input_thread.start()
            self.result_queue.put(("started", process))

            if stop_event.is_set():
                self._terminate_process_tree(process)

            def pump(stream, channel):
                decoder = codecs.getincrementaldecoder("utf-8")("replace")
                try:
                    while True:
                        data = os.read(stream.fileno(), 4096)
                        if not data:
                            break
                        chunk = decoder.decode(data)
                        if chunk:
                            self.result_queue.put(("stream", channel, chunk))
                    tail = decoder.decode(b"", final=True)
                    if tail:
                        self.result_queue.put(("stream", channel, tail))
                except Exception as exc:
                    self.result_queue.put(
                        ("stream", "error", f"\n[Output read error] {exc}\n")
                    )
                finally:
                    try:
                        stream.close()
                    except Exception:
                        pass

            stdout_thread = threading.Thread(
                target=pump, args=(process.stdout, "stdout"), daemon=True
            )
            stderr_thread = threading.Thread(
                target=pump, args=(process.stderr, "stderr"), daemon=True
            )
            stdout_thread.start()
            stderr_thread.start()
            exit_code = process.wait()
            self.stdin_queue.put(None)
            input_thread.join(timeout=1.0)
            stdout_thread.join(timeout=2.0)
            stderr_thread.join(timeout=2.0)
            if stdout_thread.is_alive() or stderr_thread.is_alive():
                self._terminate_process_tree(process, force=True)
                stdout_thread.join(timeout=1.0)
                stderr_thread.join(timeout=1.0)
                if stdout_thread.is_alive() or stderr_thread.is_alive():
                    self.result_queue.put(
                        (
                            "stream",
                            "error",
                            "\n[Output warning] A child process kept an output pipe open.\n",
                        )
                    )
            if stop_event.is_set():
                status = "stopped"
        except Exception as exc:
            status = "error"
            self.result_queue.put(("stream", "error", f"\n[Run error] {exc}\n"))
            if process is not None:
                self._terminate_process_tree(process, force=True)
                if process.poll() is None:
                    try:
                        process.wait(timeout=2.0)
                    except Exception:
                        pass
                try:
                    if process.stdin:
                        process.stdin.close()
                except Exception:
                    pass
            try:
                self.stdin_queue.put_nowait(None)
            except Exception:
                pass
        finally:
            if cleanup_path is not None:
                try:
                    cleanup_path.unlink(missing_ok=True)
                except Exception:
                    pass
            self.result_queue.put(("finished", status, exit_code, argument))

    CONTROL_PATTERN = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")

    def _ansi_tag(self, color):
        name = "ansi_" + color[1:]
        if name not in self.ansi_tags:
            self.output.tag_configure(name, foreground=color)
            self.ansi_tags.add(name)
        return name

    def _xterm_color(self, index):
        if index < 8:
            return self.ANSI_COLORS[30 + index]
        if index < 16:
            return self.ANSI_COLORS[90 + index - 8]
        if index < 232:
            index -= 16
            levels = [
                0 if value == 0 else 55 + 40 * value
                for value in (index // 36, (index // 6) % 6, index % 6)
            ]
            return "#%02x%02x%02x" % tuple(levels)
        if index < 256:
            level = 8 + (index - 232) * 10
            return "#%02x%02x%02x" % (level, level, level)
        return None

    def _apply_sgr(self, params):
        codes = (
            [int(part) if part.isdigit() else 0 for part in params.split(";")]
            if params
            else [0]
        )
        i = 0
        while i < len(codes):
            code = codes[i]
            if code in (0, 39):
                self.ansi_color = None
            elif code in self.ANSI_COLORS:
                self.ansi_color = self.ANSI_COLORS[code]
            elif code in (38, 48):
                if i + 2 < len(codes) and codes[i + 1] == 5:
                    if code == 38:
                        self.ansi_color = self._xterm_color(codes[i + 2])
                    i += 2
                elif i + 4 < len(codes) and codes[i + 1] == 2:
                    if code == 38:
                        red, green, blue = (
                            min(255, (min(value, 255) >> 5) * 37)
                            for value in codes[i + 2 : i + 5]
                        )
                        self.ansi_color = "#%02x%02x%02x" % (red, green, blue)
                    i += 4
                else:
                    break
            i += 1

    def _apply_escape(self, params, final):
        if final == "m":
            self._apply_sgr(params)
        elif final == "K":
            if params in ("1", "2"):
                self.output.delete("end-1c linestart", "end-1c")
        elif final == "J":
            if params in ("2", "3"):
                self.output.delete("1.0", "end-1c")

    def _terminal_text(self, segment, tag):
        segment = self.CONTROL_PATTERN.sub("", segment)
        if not segment:
            return
        names = []
        if tag:
            names.append(tag)
        if self.ansi_color:
            names.append(self._ansi_tag(self.ansi_color))
        tags = tuple(names)
        for part in re.split(r"(\r\n|\r|\n)", segment):
            if not part:
                continue
            if part == "\r":
                self.carriage_return = True
            elif part in ("\n", "\r\n"):
                self.carriage_return = False
                self.output.insert("end-1c", "\n", tags)
            else:
                if self.carriage_return:
                    self.carriage_return = False
                    self.output.delete("end-1c linestart", "end-1c")
                self.output.insert("end-1c", part, tags)

    def _terminal_write(self, text, tag=None):
        if not text:
            return
        if self.temp_arg:
            text = text.replace(self.temp_arg, "current code")
        if self.temp_path:
            text = text.replace(str(self.temp_path), "current code")
        text = self.ansi_tail + text
        self.ansi_tail = ""
        cut = text.rfind("\x1b")
        if cut != -1 and re.fullmatch(r"\x1b(\[[0-9;?]*)?", text[cut:]):
            self.ansi_tail = text[cut:]
            text = text[:cut]
            if not text:
                return
        at_bottom = self.output.yview()[1] >= 0.999
        pending_input = ""
        if self._input_enabled():
            try:
                pending_input = self.output.get("terminal_input_start", "end-1c")
                self.output.delete("terminal_input_start", "end-1c")
            except tk.TclError:
                pending_input = ""
        position = 0
        for match in self.ANSI_PATTERN.finditer(text):
            self._terminal_text(text[position : match.start()], tag)
            self._apply_escape(match.group(1), match.group(2))
            position = match.end()
        self._terminal_text(text[position:], tag)
        if self._input_enabled():
            self.output.mark_set("terminal_input_start", "end-1c")
            self.output.mark_gravity("terminal_input_start", "left")
            if pending_input:
                self.output.insert("end-1c", pending_input)
        overflow = (
            int(self.output.index("end-1c").split(".")[0]) - self.MAX_OUTPUT_LINES
        )
        if overflow > 0:
            self.output.delete("1.0", f"{overflow + 1}.0")
        self.output.mark_set(tk.INSERT, "end-1c")
        if at_bottom:
            self.output.see(tk.END)

    def _input_enabled(self):
        return self.running or self.command_input_active

    def _prepare_command_input(self, prefill=""):
        if self.running or self.command_input_active:
            return
        output = self.output
        if output.index("end-1c") != "1.0" and output.get("end-2c", "end-1c") != "\n":
            output.insert("end-1c", "\n")
        output.insert("end-1c", "> ")
        output.mark_set("terminal_input_start", "end-1c")
        output.mark_gravity("terminal_input_start", "left")
        if prefill:
            output.insert("end-1c", prefill)
        output.mark_set(tk.INSERT, "end-1c")
        self.command_input_active = True
        self.history_index = len(self.command_history)
        self.history_draft = ""
        self.set_terminal_cursor(True)
        output.see(tk.END)

    def terminal_copy(self, event=None):
        try:
            selected = self.output.get("sel.first", "sel.last")
        except tk.TclError:
            return "break"
        self.root.clipboard_clear()
        self.root.clipboard_append(selected)
        return "break"

    def terminal_select_all(self, event=None, everything=False):
        self.output.tag_remove("sel", "1.0", tk.END)
        if self._input_enabled() and not everything:
            self.output.tag_add("sel", "terminal_input_start", "end-1c")
        else:
            self.output.tag_add("sel", "1.0", "end-1c")
        return "break"

    def terminal_paste(self, event=None):
        if not self._input_enabled():
            return "break"
        try:
            text = self.root.clipboard_get()
        except tk.TclError:
            return "break"
        parts = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
        self.output.tag_remove("sel", "1.0", tk.END)
        try:
            if self.output.compare(tk.INSERT, "<", "terminal_input_start"):
                self.output.mark_set(tk.INSERT, "end-1c")
        except tk.TclError:
            self.output.mark_set(tk.INSERT, "end-1c")
        for index, part in enumerate(parts):
            if index > 0:
                self.output.mark_set(tk.INSERT, "end-1c")
            if part and self._input_enabled():
                self.output.insert(tk.INSERT, part)
            if index < len(parts) - 1:
                self.submit_terminal_input()
        self.output.see(tk.END)
        return "break"

    def show_terminal_menu(self, event):
        has_selection = bool(self.output.tag_ranges("sel"))
        self.terminal_menu.entryconfigure(
            0, state=tk.NORMAL if has_selection else tk.DISABLED
        )
        self.terminal_menu.entryconfigure(
            1, state=tk.NORMAL if self._input_enabled() else tk.DISABLED
        )
        try:
            self.terminal_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.terminal_menu.grab_release()
        return "break"

    def terminal_clear_screen(self):
        pending = ""
        if self._input_enabled():
            try:
                pending = self.output.get("terminal_input_start", "end-1c")
            except tk.TclError:
                pending = ""
        was_running = self.running
        self._set_output("")
        if was_running:
            self.output.mark_set("terminal_input_start", "end-1c")
            self.output.mark_gravity("terminal_input_start", "left")
            if pending:
                self.output.insert("end-1c", pending)
        else:
            self.command_input_active = False
            self._prepare_command_input(pending)
        return "break"

    def _history(self):
        return self.input_history if self.running else self.command_history

    def history_navigate(self, direction):
        history = self._history()
        if not history:
            return "break"
        if self.history_index >= len(history):
            if direction > 0:
                return "break"
            self.history_draft = self.output.get("terminal_input_start", "end-1c")
        self.history_index = max(0, min(len(history), self.history_index + direction))
        text = (
            self.history_draft
            if self.history_index == len(history)
            else history[self.history_index]
        )
        self.output.delete("terminal_input_start", "end-1c")
        self.output.insert("end-1c", text)
        self.output.mark_set(tk.INSERT, "end-1c")
        self.output.see(tk.END)
        return "break"

    def terminal_send_eof(self):
        if self.process is not None and self.process.poll() is None:
            self.stdin_queue.put(None)
            self.status_var.set("Sent end-of-file (EOF)")
        return "break"

    def on_terminal_keypress(self, event):
        shortcut_mask = 0x8 if sys.platform == "darwin" else 0x4
        control = bool(event.state & 0x4)
        shift = bool(event.state & 0x1)
        key = event.keysym.lower()
        if event.state & shortcut_mask:
            action = self._global_action(key, shift)
            if action is not None:
                action()
                return "break"
            if key == "tab":
                return self.cycle_document(-1 if shift else 1)
        if control and key == "c":
            try:
                selected = self.output.get("sel.first", "sel.last")
            except tk.TclError:
                selected = ""
            if selected:
                self.root.clipboard_clear()
                self.root.clipboard_append(selected)
                return "break"
            if self.running:
                self.stop_code()
                return "break"
            if self.command_input_active:
                self.output.delete("terminal_input_start", "end-1c")
                return "break"
        if control and key == "l":
            return self.terminal_clear_screen()
        if event.keysym in ("Prior", "Next"):
            self.output.yview_scroll(-1 if event.keysym == "Prior" else 1, "pages")
            return "break"
        if not self._input_enabled():
            if control and key == "a":
                return self.terminal_select_all()
            if control and key == "c":
                return None
            return "break"
        try:
            if self.output.compare(tk.INSERT, "<", "terminal_input_start"):
                self.output.mark_set(tk.INSERT, "end-1c")
        except tk.TclError:
            self.output.mark_set("terminal_input_start", "end-1c")
            self.output.mark_set(tk.INSERT, "end-1c")
        if event.keysym == "Tab" and self.command_input_active:
            return self.terminal_complete()
        if event.keysym == "Up":
            return self.history_navigate(-1)
        if event.keysym == "Down":
            return self.history_navigate(1)
        if control and key == "u":
            self.output.delete("terminal_input_start", tk.INSERT)
            return "break"
        if control and key == "d":
            if self.running and not self.output.get("terminal_input_start", "end-1c"):
                return self.terminal_send_eof()
            return "break"
        if control and key == "a":
            return self.terminal_select_all()
        if event.keysym == "Home":
            self.output.mark_set(tk.INSERT, "terminal_input_start")
            return "break"
        if event.keysym == "End":
            self.output.mark_set(tk.INSERT, "end-1c")
            return "break"
        if event.keysym == "Left" and self.output.compare(
            tk.INSERT, "<=", "terminal_input_start"
        ):
            return "break"
        if event.char and event.char >= " " and not control:
            # Typing over a selection that includes earlier output would delete that output.
            self.output.tag_remove("sel", "1.0", tk.END)
            self.output.see(tk.END)
        return None

    def terminal_click(self, event):
        if self._input_enabled():
            self.root.after_idle(lambda: self.output.mark_set(tk.INSERT, "end-1c"))

    def terminal_backspace(self, event=None):
        if not self._input_enabled():
            return "break"
        try:
            if self.output.compare(tk.INSERT, "<=", "terminal_input_start"):
                return "break"
            self.output.delete("insert-1c", tk.INSERT)
            return "break"
        except tk.TclError:
            return "break"

    def submit_terminal_input(self, event=None):
        if self.command_input_active and not self.running:
            value = self.output.get("terminal_input_start", "end-1c")
            self.output.insert("end-1c", "\n")
            self.command_input_active = False
            self.output.mark_set("terminal_input_start", "end-1c")
            self.output.mark_gravity("terminal_input_start", "left")
            self.output.mark_set(tk.INSERT, "end-1c")
            # F5 echoes a full command but runs the current tab; only a command that names a real
            # file is worth recalling from history.
            override, replayable = self.command_override or (None, True)
            self.command_override = None
            if (
                value.strip()
                and replayable
                and (not self.command_history or self.command_history[-1] != value)
            ):
                self.command_history.append(value)
                del self.command_history[:-200]
            self.run_shell_command(value, override)
            return "break"
        if not self.running:
            return "break"
        process = self.process
        try:
            value = self.output.get("terminal_input_start", "end-1c")
            self.output.insert("end-1c", "\n")
            self.output.mark_set("terminal_input_start", "end-1c")
            self.output.mark_gravity("terminal_input_start", "left")
            self.output.mark_set(tk.INSERT, "end-1c")
            if value.strip() and (
                not self.input_history or self.input_history[-1] != value
            ):
                self.input_history.append(value)
                del self.input_history[:-200]
            self.history_index = len(self.input_history)
            self.history_draft = ""
            if process is None:
                self.pending_input_lines.append(value)
            elif process.poll() is not None:
                self.status_var.set("The program is no longer accepting input")
            else:
                self.stdin_queue.put(value)
            self.output.see(tk.END)
        except (BrokenPipeError, OSError, ValueError):
            self.status_var.set("The program is no longer accepting input")
        return "break"

    def _load_engine_flags(self):
        try:
            key = (str(self.engine_path), self.engine_path.stat().st_mtime)
        except OSError:
            key = (str(self.engine_path), None)
        if key == self.flag_cache_key:
            return
        self.flag_cache_key = key
        self.flag_info = {}
        self.engine_help_text = ""
        options = {
            "stdin": subprocess.DEVNULL,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.STDOUT,
        }
        if os.name == "nt":
            options["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            result = subprocess.run(
                [str(self.engine_path), "--help"], timeout=3, **options
            )
            self.engine_help_text = result.stdout.decode("utf-8", "replace").replace(
                "\r\n", "\n"
            )
        except (OSError, subprocess.SubprocessError, ValueError):
            return
        for line in self.engine_help_text.split("\n"):
            if not re.match(r"\s+-", line):
                continue
            head = re.split(r"\s{2,}", line.strip(), maxsplit=1)[0]
            for part in head.split(","):
                words = part.strip().split()
                if words and words[0].startswith("-"):
                    self.flag_info[words[0]] = any("<" in word for word in words[1:])

    def _known_flags(self):
        self._load_engine_flags()
        return self.flag_info or self.FALLBACK_FLAGS

    def _parse_run_args(self, args):
        info = self._known_flags()
        flags, files = [], []
        literal = False
        index = 0
        while index < len(args):
            token = args[index]
            if literal or not token.startswith("-") or token == "-":
                files.append(token)
            elif token == "--":
                literal = True
            else:
                flags.append(token)
                if (
                    "=" not in token
                    and info.get(token, False)
                    and index + 1 < len(args)
                ):
                    index += 1
                    flags.append(args[index])
            index += 1
        if len(files) > 1:
            raise ValueError("Only one script file can be specified")
        return flags, files

    @staticmethod
    def _split_command(line):
        tokens, current = [], []
        quote = None
        started = False
        for char in line:
            if quote:
                if char == quote:
                    quote = None
                else:
                    current.append(char)
            elif char in "\"'":
                quote = char
                started = True
            elif char.isspace():
                if started:
                    tokens.append("".join(current))
                    current, started = [], False
            else:
                current.append(char)
                started = True
        if quote:
            raise ValueError("Unclosed quote")
        if started:
            tokens.append("".join(current))
        return tokens

    def _print(self, text="", tag=None):
        self._terminal_write(text + "\n", tag)

    def _resolve_path(self, name, want="any"):
        path = Path(os.path.expanduser(name))
        if not path.is_absolute():
            path = self._terminal_cwd() / path
        candidates = [path]
        if want != "dir" and not path.suffix:
            candidates.append(path.with_name(path.name + ".cuff"))
        for candidate in candidates:
            if want == "dir" and candidate.is_dir():
                return candidate.resolve()
            if want != "dir" and candidate.is_file():
                return candidate.resolve()
        if want != "dir" and path.is_dir():
            raise ValueError(f"Path is a directory: {name}")
        raise ValueError(
            f"Could not find {'directory' if want == 'dir' else 'file'}: {name}"
        )

    def _command_table(self):
        return {
            "RUN": (
                lambda args: self.cmd_run(args),
                "[file] [options]",
                "Run a file (the current tab when no file is given)",
            ),
            "AST": (
                lambda args: self.cmd_run(args, ["--ast"]),
                "[file] [options]",
                "Print tokens and syntax tree without running",
            ),
            "BYTECODE": (
                lambda args: self.cmd_run(args, ["--bytecode"]),
                "[file] [options]",
                "Run using bytecode",
            ),
            "SET": (
                self.cmd_set,
                "[options | --reset]",
                "Set persistent engine options",
            ),
            "FLAGS": (self.cmd_flags, "", "Show options supported by the engine"),
            "CD": (self.cmd_cd, "[folder]", "Change the terminal working folder"),
            "PWD": (
                lambda args: self._print(str(self._terminal_cwd())),
                "",
                "Show the working folder",
            ),
            "LS": (self.cmd_ls, "[-a] [folder]", "List folder contents"),
            "CAT": (self.cmd_cat, "<file>", "Print a file"),
            "OPEN": (self.cmd_open, "<file>...", "Open files in tabs"),
            "NEW": (lambda args: self.new_file(), "", "Create a new tab"),
            "CLEAR": (lambda args: self.terminal_clear_screen(), "", "Clear output"),
            "ENGINE": (self.cmd_engine, "[path]", "Show or change the compiler"),
            "HISTORY": (self.cmd_history, "", "Show command history"),
            "HELP": (
                self.cmd_help,
                "[command]",
                "Show the command list, or one command",
            ),
        }

    COMMAND_ALIASES = {"CLS": "CLEAR", "DIR": "LS", "TYPE": "CAT", "?": "HELP"}

    def _resolve_command(self, token):
        """An IDE command is exactly '%' + a capitalised name (or alias), such as %RUN.
        Anything else (run, %run, %Run) is not a command and returns None."""
        if not token.startswith("%") or len(token) < 2:
            return None
        head = token[1:]
        name = self.COMMAND_ALIASES.get(head, head)
        return name if name in self._command_table() else None

    def _usage_text(self, name):
        """One format everywhere: %NAME <required> [optional]."""
        usage = self._command_table()[name][1]
        return f"%{name} {usage}".rstrip()

    def run_shell_command(self, line, override=None):
        try:
            if override is not None:
                override()
                return
            tokens = self._split_command(line)
            if not tokens:
                return
            name = self._resolve_command(tokens[0])
            if name is None:
                self._print(
                    f"Unknown command: {tokens[0]} (commands are written as %NAME in "
                    "capitals, such as %RUN; enter %HELP for the list)",
                    "error",
                )
                return
            self._command_table()[name][0](tokens[1:])
        except ValueError as exc:
            self._print(str(exc), "error")
        except Exception as exc:
            self._print(f"Command failed: {exc}", "error")
        finally:
            if not self.running:
                self._prepare_command_input()

    def cmd_run(self, args, extra=()):
        flags, files = self._parse_run_args(args) if args else ([], [])
        path = self._resolve_path(files[0]) if files else None
        self.start_run(path, [*extra, *flags], clear=False)

    def cmd_set(self, args):
        if args in (["--reset"], ["reset"]):
            self.run_flags = []
        elif args:
            flags, files = self._parse_run_args(args)
            if files:
                raise ValueError(
                    "%SET accepts options only, for example: %SET --timeout 2000"
                )
            self.run_flags = flags
        shown = " ".join(self._quote_argument(item) for item in self.run_flags)
        self._print(f"Run options: {shown or '(none)'}")

    def cmd_flags(self, args):
        self._load_engine_flags()
        if self.engine_help_text.strip():
            self._print(self.engine_help_text.rstrip("\n"))
        else:
            self._print(
                "Could not load engine options. Check the compiler path.", "error"
            )

    def cmd_cd(self, args):
        if not args:
            self._print(str(self._terminal_cwd()))
            return
        self.shell_cwd = self._resolve_path(args[0], "dir")
        self._print(str(self.shell_cwd))

    def cmd_ls(self, args):
        show_hidden = "-a" in args
        names = [item for item in args if item != "-a"]
        directory = (
            self._resolve_path(names[0], "dir") if names else self._terminal_cwd()
        )
        try:
            entries = sorted(
                directory.iterdir(),
                key=lambda item: (not item.is_dir(), item.name.lower()),
            )
        except OSError as exc:
            raise ValueError(f"Could not read directory: {exc}")
        shown = 0
        for entry in entries:
            if entry.name.startswith(".") and not show_hidden:
                continue
            if shown >= 500:
                self._print("... (showing the first 500 entries)")
                break
            if entry.is_dir():
                self._print(f"\x1b[94m{entry.name}/\x1b[0m")
            elif entry.suffix.lower() == ".cuff":
                self._print(f"\x1b[92m{entry.name}\x1b[0m")
            else:
                self._print(entry.name)
            shown += 1
        if shown == 0:
            self._print("(empty)")

    def cmd_cat(self, args):
        if not args:
            raise ValueError("Usage: " + self._usage_text("CAT"))
        path = self._resolve_path(args[0])
        try:
            with open(path, "rb") as file:
                data = file.read(1_000_000)
        except OSError as exc:
            raise ValueError(f"Could not read file: {exc}")
        text = data.decode("utf-8", "replace").replace("\r\n", "\n").replace("\x1b", "")
        lines = text.split("\n")
        for number, line in enumerate(lines[:400], 1):
            self._print(f"\x1b[90m{number:>4}\x1b[0m  {line}")
        if len(lines) > 400:
            self._print("... (showing the first 400 lines)")

    def cmd_open(self, args):
        if not args:
            raise ValueError("Usage: " + self._usage_text("OPEN"))
        for name in args:
            self.load_file(self._resolve_path(name))

    def cmd_engine(self, args):
        if args:
            path = Path(os.path.expanduser(args[0]))
            if not path.is_absolute():
                path = self._terminal_cwd() / path
            if not path.is_file():
                raise ValueError(f"File not found: {args[0]}")
            self.engine_path = path.resolve()
            self.update_engine_status()
        self._print(
            f"Compiler: {self.engine_path}"
            + ("" if self.engine_path.is_file() else " (not found)")
        )

    def cmd_history(self, args):
        if not self.command_history:
            self._print("(no history)")
        for number, command in enumerate(self.command_history, 1):
            self._print(f"{number:>4}  {command}")

    def cmd_help(self, args):
        table = self._command_table()
        if args:
            name = self._resolve_command(
                args[0] if args[0].startswith("%") else "%" + args[0]
            )
            if name is None:
                raise ValueError(f"Unknown command: {args[0]}")
            self._print(f"Usage: {self._usage_text(name)}")
            self._print(f"  {table[name][2]}")
            return
        self._print(
            "<required>  [optional]  -  commands start with % and are written in capitals, e.g. %RUN.",
            "hint",
        )
        width = max(len(self._usage_text(name)) for name in table) + 2
        for name, (_handler, _usage, description) in table.items():
            self._print(f"  {self._usage_text(name).ljust(width)}{description}")

    def _completion_candidates(self, before_tokens, partial):
        if not before_tokens:
            # Completion always offers the exact spelling that will be recognised.
            bare = partial[1:] if partial.startswith("%") else partial
            return [
                "%" + name
                for name in self._command_table()
                if name.startswith(bare.upper())
            ]
        command = self._resolve_command(before_tokens[0])
        if command is None:
            return []
        if partial.startswith("-") and command in ("RUN", "AST", "BYTECODE", "SET"):
            return sorted(
                flag for flag in self._known_flags() if flag.startswith(partial)
            )
        if command in ("PWD", "NEW", "CLEAR", "HISTORY", "HELP", "FLAGS"):
            return []
        base_length = len(partial) - len(os.path.basename(partial.replace("\\", "/")))
        base, prefix = partial[:base_length], partial[base_length:]
        directory = Path(os.path.expanduser(base)) if base else Path(".")
        if not directory.is_absolute():
            directory = self._terminal_cwd() / directory
        try:
            entries = list(directory.iterdir())
        except OSError:
            return []
        found = []
        for entry in entries:
            name = entry.name
            if name.startswith(".") and not prefix.startswith("."):
                continue
            if not os.path.normcase(name).startswith(os.path.normcase(prefix)):
                continue
            is_dir = entry.is_dir()
            if command == "CD" and not is_dir:
                continue
            if (
                command in ("RUN", "AST", "BYTECODE", "OPEN")
                and not is_dir
                and entry.suffix.lower() != ".cuff"
            ):
                continue
            found.append(base + name + (os.sep if is_dir else ""))
        return sorted(found, key=str.lower)

    def terminal_complete(self):
        text = self.output.get("terminal_input_start", "insert")
        match = re.search(r'"[^"]*$|[^\s"]*$', text)
        token = match.group(0)
        start = match.start()
        quoted = token.startswith('"')
        partial = token[1:] if quoted else token
        try:
            before_tokens = self._split_command(text[:start])
        except ValueError:
            return "break"
        candidates = self._completion_candidates(before_tokens, partial)
        if not candidates:
            self.root.bell()
            return "break"
        common = (
            os.path.commonprefix(candidates) if len(candidates) > 1 else candidates[0]
        )
        if len(common) > len(partial) or len(candidates) == 1:
            needs_quote = quoted or any(char.isspace() for char in common)
            replacement = ('"' if needs_quote else "") + common
            if len(candidates) == 1 and not common.endswith(os.sep):
                replacement += ('"' if needs_quote else "") + " "
            self.output.delete(f"terminal_input_start+{start}c", tk.INSERT)
            self.output.insert(tk.INSERT, replacement)
            self.output.see(tk.END)
            return "break"
        current = self.output.get("terminal_input_start", "end-1c")
        self.output.delete("terminal_input_start", "end-1c")
        self.output.insert("end-1c", current + "\n")
        self.command_input_active = False
        lines, row = [], ""
        for name in candidates:
            label = os.path.basename(name.rstrip("/\\")) + (
                os.sep if name.endswith(os.sep) else ""
            )
            if name.startswith("-") or not before_tokens:
                label = name
            if row and len(row) + len(label) + 2 > 80:
                lines.append(row)
                row = ""
            row += label + "  "
        if row:
            lines.append(row.rstrip())
        self.output.insert("end-1c", "\n".join(lines) + "\n")
        self._prepare_command_input(current)
        return "break"

    def _poll_run_result(self):
        if self.closing:
            return
        finished = None
        grouped = []
        while True:
            try:
                event = self.result_queue.get_nowait()
            except queue.Empty:
                break
            if event[0] == "started":
                self.process = event[1]
                pending = self.pending_input_lines[:]
                self.pending_input_lines.clear()
                for value in pending:
                    self.stdin_queue.put(value)
            elif event[0] == "input_error":
                self.status_var.set("The program is no longer accepting input")
            elif event[0] == "stream":
                channel = event[1]
                tag = "error" if channel == "stderr" or channel == "error" else None
                if grouped and grouped[-1][0] == tag:
                    grouped[-1][1] += event[2]
                else:
                    grouped.append([tag, event[2]])
            elif event[0] == "finished":
                finished = event[1:]
        for tag, value in grouped:
            self._terminal_write(value, tag)
        if finished is None:
            self.poll_job = self.root.after(40, self._poll_run_result)
            return

        result_status, exit_code, temp_name = finished
        self.poll_job = None
        self.process = None
        self.running = False
        self.worker_thread = None
        self.set_terminal_cursor(False)
        elapsed = (
            self.format_elapsed(time.monotonic() - self.run_started)
            if self.run_started is not None
            else ""
        )
        self.run_started = None
        if result_status == "stopped":
            message = "Stopped"
        elif result_status == "error":
            message = "Run Error"
        elif exit_code == 0:
            message = "Finished"
        elif exit_code is not None:
            message = f"Exited with code {exit_code}"
        else:
            message = "Exited"
        self.status_var.set(f"{message} · {elapsed}" if elapsed else message)
        self._prepare_command_input()
        self.output.see(tk.END)
        self.output.mark_set(tk.INSERT, "end-1c")
        self.update_title()

    @staticmethod
    def format_elapsed(seconds):
        if seconds < 60:
            return f"{seconds:.2f}s"
        minutes, rest = divmod(seconds, 60)
        return f"{int(minutes)}m {rest:.1f}s"

    def stop_code(self, event=None):
        if not self.running:
            return "break"
        if self.stop_event is not None:
            self.stop_event.set()
        process = self.process
        if process is not None:
            self._terminate_process_tree(process)
            self.root.after(900, self._force_kill_process, process)
        self.status_var.set("Stopping process...")
        return "break"

    def _terminate_process_tree(self, process, force=False):
        try:
            if os.name == "nt":
                if process.poll() is None:
                    command = ["taskkill", "/PID", str(process.pid), "/T"]
                    if force:
                        command.append("/F")
                    subprocess.Popen(
                        command,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                    )
                    if not force:
                        try:
                            process.terminate()
                        except OSError:
                            pass
            else:
                os.killpg(process.pid, signal.SIGKILL if force else signal.SIGTERM)
        except (OSError, subprocess.SubprocessError):
            if process.poll() is None:
                try:
                    process.kill() if force else process.terminate()
                except OSError:
                    pass

    def _force_kill_process(self, process):
        if process.poll() is None:
            self._terminate_process_tree(process, force=True)

    def _parse_error_header(self, line_text):
        match = re.search(
            r"\bat line\s+(\d+)\s*,\s*column\s+(\d+)", line_text, re.IGNORECASE
        )
        if not match:
            return None
        path_match = re.search(r"\bin\s+'([^']+)'", line_text)
        source_path = path_match.group(1) if path_match else None
        return int(match.group(1)), int(match.group(2)), source_path

    def _error_context(self, output_line):
        for line_no in range(output_line, max(0, output_line - 18), -1):
            text = self.output.get(f"{line_no}.0", f"{line_no}.end")
            parsed = self._parse_error_header(text)
            if parsed:
                return parsed
        return None

    def extract_error_line(self, line_text):
        parsed = self._parse_error_header(line_text)
        if parsed:
            return parsed
        snippet = re.match(r"\s*(\d+)\s*\|", line_text)
        if snippet:
            return int(snippet.group(1)), None, None
        return None

    def _error_target_document(self, source_path):
        run_doc = self.last_run_doc if self.last_run_doc in self.docs else None
        if source_path is None or source_path == "current code":
            if run_doc is not None:
                return run_doc, None
            if self.last_run_path is None:
                return None, "The tab used for this run is closed"
            source_path = str(self.last_run_path)
        try:
            candidate = Path(source_path)
            if not candidate.is_absolute():
                candidate = self.last_run_dir / candidate
            candidate = candidate.resolve()
        except (OSError, ValueError):
            return None, "The error came from another file"
        existing = self._find_document(candidate)
        if existing is not None:
            return existing, None
        if candidate.is_file():
            self.load_file(candidate)
            return self._find_document(candidate), None
        return None, "The error came from another file"

    def go_to_error_line(self, event):
        try:
            output_index = self.output.index(f"@{event.x},{event.y}")
            output_line = int(output_index.split(".")[0])
            clicked_text = self.output.get(f"{output_line}.0", f"{output_line}.end")
            clicked = self.extract_error_line(clicked_text)
            context = self._error_context(output_line)
            if clicked and clicked[2] is not None:
                source_line, source_column, source_path = clicked
            elif clicked and clicked[1] is None:
                source_line = clicked[0]
                source_column = context[1] if context else None
                source_path = context[2] if context else None
            elif context:
                source_line, source_column, source_path = context
            else:
                return
            doc, reason = self._error_target_document(source_path)
            if doc is None:
                self.status_var.set(reason or "Could not locate the error")
                return
            if (
                doc is self.last_run_doc
                and doc.editor.get("1.0", "end-1c") != self.last_run_source
            ):
                self.status_var.set(
                    "The source changed after the run; cannot navigate to the error"
                )
                return
            self.select_document(doc)
            last_line = int(self.editor.index("end-1c").split(".")[0])
            if not 1 <= source_line <= last_line:
                return
            self.editor.focus_set()
            column_index = max(0, (source_column or 1) - 1)
            self.editor.mark_set(tk.INSERT, f"{source_line}.{column_index}")
            self.editor.see(f"{source_line}.{column_index}")
            self.editor.tag_remove("sel", "1.0", tk.END)
            self.editor.tag_remove("error_line", "1.0", tk.END)
            self.editor.tag_add("error_line", f"{source_line}.0", f"{source_line}.end")
            self.update_status()
        except (tk.TclError, ValueError):
            return

    def on_output_motion(self, event):
        try:
            line_number = int(self.output.index(f"@{event.x},{event.y}").split(".")[0])
            line_text = self.output.get(f"{line_number}.0", f"{line_number}.end")
            clickable = (
                self.extract_error_line(line_text) is not None
                or self._error_context(line_number) is not None
            )
            self.output.config(cursor="hand2" if clickable else "xterm")
        except tk.TclError:
            self.output.config(cursor="xterm")

    def close_window(self):
        if self.closing:
            return
        for doc in list(self.docs):
            if not self.confirm_close(doc):
                return
        if self.running:
            self.closing = True
            if self.stop_event is not None:
                self.stop_event.set()
            if self.process is not None:
                self._terminate_process_tree(self.process)
                if self.process.poll() is None:
                    try:
                        self.process.wait(timeout=0.5)
                    except (OSError, subprocess.TimeoutExpired):
                        self._terminate_process_tree(self.process, force=True)
        if self.highlight_job is not None:
            try:
                self.root.after_cancel(self.highlight_job)
            except tk.TclError:
                pass
        if self.poll_job is not None:
            try:
                self.root.after_cancel(self.poll_job)
            except tk.TclError:
                pass
        self.root.destroy()


def main():
    # A windowed (console=False) build has no stdout/stderr; code that writes to them must not crash.
    for stream_name in ("stdout", "stderr"):
        if getattr(sys, stream_name) is None:
            setattr(sys, stream_name, open(os.devnull, "w", encoding="utf-8"))
    if sys.version_info[:2] < MINIMUM_PYTHON_VERSION:
        current_version = ".".join(str(part) for part in sys.version_info[:3])
        message = (
            "CuffScript IDE requires Python 3.8 or later.\n"
            f"Current version: Python {current_version}\n"
            "Update Python and try again."
        )
        try:
            import ctypes

            if os.name == "nt":
                ctypes.windll.user32.MessageBoxW(0, message, "CuffScript IDE", 0x10)
            else:
                sys.stderr.write(message + "\n")
        except Exception:
            sys.stderr.write(message + "\n")
        return 1
    if os.name == "nt":
        # Without this, Windows scales the whole window as a bitmap on high-DPI screens: blurry text, clipped labels.
        try:
            import ctypes

            try:
                ctypes.windll.shcore.SetProcessDpiAwareness(1)
            except (AttributeError, OSError):
                ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass
    try:
        root = tk.Tk()
    except tk.TclError as exc:
        message = (
            "Could not start the Tkinter interface. Make sure Tcl/Tk is installed. "
            "On Linux, verify that a graphical session (DISPLAY or WAYLAND_DISPLAY) is available.\n\n"
            f"Details: {exc}"
        )
        try:
            import ctypes

            if os.name == "nt":
                ctypes.windll.user32.MessageBoxW(0, message, "CuffScript IDE", 0x10)
            else:
                sys.stderr.write(message + "\n")
        except Exception:
            sys.stderr.write(message + "\n")
        return 1
    holder = {"app": None, "shown": False}

    def report_error(exc_type, exc_value, exc_tb):
        # A bug in one callback should not take the whole editor (and unsaved tabs) down.
        details = "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
        try:
            sys.stderr.write(details)
        except Exception:
            pass
        app = holder["app"]
        try:
            if app is not None:
                app.status_var.set(f"Internal error: {exc_value}")
            if not holder["shown"]:
                holder["shown"] = True
                messagebox.showerror(
                    "Internal Error",
                    "Something went wrong, but the IDE is still running.\n"
                    "Consider saving your work and restarting.\n\n"
                    f"{exc_type.__name__}: {exc_value}",
                    parent=root,
                )
        except Exception:
            pass

    root.report_callback_exception = report_error
    try:
        holder["app"] = CuffScriptIDE(root)
        root.mainloop()
    except Exception as exc:
        try:
            messagebox.showerror(
                "IDE Error",
                f"An error occurred while starting or running the IDE.\n{exc}",
                parent=root,
            )
        except tk.TclError:
            sys.stderr.write(f"CuffScript IDE error: {exc}\n")
        try:
            root.destroy()
        except tk.TclError:
            pass
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
