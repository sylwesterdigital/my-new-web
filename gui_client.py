# gui_client.py — LIST + GET/HEAD with optional TYPE header, text or image preview
# Requires: Python 3.7+
# Optional: Pillow for image preview →  python3 -m pip install pillow

import socket, io, sys, tempfile, os, subprocess, tkinter as tk
from tkinter import ttk, messagebox, filedialog

# Optional image support
try:
    from PIL import Image, ImageTk
    PIL_OK = True
except Exception:
    PIL_OK = False

# -------------------- wire protocol helpers --------------------

def _recv_line(sock) -> bytes:
    out = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            break
        out += ch
        if ch == b"\n":
            break
    return bytes(out)

def list_files(host: str, port: str):
    with socket.create_connection((host, int(port)), timeout=5) as s:
        s.sendall(b"LIST\n")
        head = _recv_line(s)  # b"FILES n\n"
        if not head.startswith(b"FILES "):
            raise RuntimeError(f"Bad LIST header: {head!r}")
        # read until blank line
        lines = bytearray()
        while True:
            ch = s.recv(1)
            if not ch:
                break
            lines += ch
            if lines.endswith(b"\n\n"):
                lines = lines[:-2]
                break
        entries = []
        # Accept "name<TAB>size" or "name<TAB>mime<TAB>size"
        for ln in lines.decode("utf-8", "replace").splitlines():
            if not ln.strip():
                continue
            parts = ln.split("\t")
            if len(parts) == 2:
                name, size = parts
                mime = ""
            elif len(parts) >= 3:
                name, mime, size = parts[0], parts[1], parts[-1]
            else:
                continue
            entries.append((name, mime, size))
        return entries

def fetch_file(host: str, port: str, name: str, head_only=False):
    """
    Returns: (data_bytes_or_b"", mime_str_or"", size_int)
    Understands optional TYPE header:
        TYPE <mime>\n
        SIZE <n>\n
        \n
        <n bytes>
    """
    with socket.create_connection((host, int(port)), timeout=5) as s:
        cmd = ("HEAD " if head_only else "GET ") + name + "\n"
        s.sendall(cmd.encode("utf-8"))

        mime = ""
        size = None

        # Read headers until blank line
        while True:
            line = _recv_line(s)
            if not line:
                break
            if line in (b"\n", b"\r\n"):
                break
            if line.startswith(b"TYPE "):
                mime = line[5:].strip().decode("utf-8", "replace")
            elif line.startswith(b"SIZE "):
                try:
                    size = int(line.split()[1])
                except Exception:
                    raise RuntimeError(f"Bad SIZE header: {line!r}")
            else:
                # ignore unknown header lines
                pass

        if size is None:
            raise RuntimeError("Missing SIZE header")

        if head_only:
            return b"", mime, size

        left = size
        data = bytearray()
        while left > 0:
            chunk = s.recv(min(65536, left))
            if not chunk:
                break
            data.extend(chunk)
            left -= len(chunk)
        return bytes(data), mime, size

# -------------------- GUI --------------------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Tiny File Client")
        self.geometry("880x560")

        # state
        self.current_name = None
        self.current_data = b""
        self.current_mime = ""
        self.tk_img = None  # keep reference for Tk

        # layout
        root = ttk.Frame(self, padding=8)
        root.pack(fill="both", expand=True)

        # top bar
        top = ttk.Frame(root); top.pack(fill="x", pady=(0,8))
        ttk.Label(top, text="Host:").pack(side="left")
        self.host = ttk.Entry(top, width=24); self.host.pack(side="left", padx=(4,10))
        ttk.Label(top, text="Port:").pack(side="left")
        self.port = ttk.Entry(top, width=7); self.port.pack(side="left", padx=(4,10))
        self.btn_refresh = ttk.Button(top, text="Connect / Refresh", command=self.refresh)
        self.btn_refresh.pack(side="left")
        if not PIL_OK:
            ttk.Label(top, text="(install Pillow for image preview)").pack(side="left", padx=12)

        # main split
        main = ttk.Frame(root); main.pack(fill="both", expand=True)
        left = ttk.Frame(main); left.pack(side="left", fill="y")
        self.files = tk.Listbox(left, width=34, activestyle="dotbox")
        self.files.pack(side="left", fill="y")
        self.files.bind("<<ListboxSelect>>", self.on_select)
        self.files.bind("<Double-Button-1>", self.on_open_preview)

        # right panel: stacked text widget and image label
        right = ttk.Frame(main); right.pack(side="left", fill="both", expand=True, padx=(10,0))
        self.text = tk.Text(right, wrap="word")
        self.text.pack(fill="both", expand=True)
        self.img_label = ttk.Label(right)  # hidden until showing an image

        # bottom bar
        bottom = ttk.Frame(root); bottom.pack(fill="x", pady=(8,0))
        ttk.Button(bottom, text="Save…", command=self.save_current).pack(side="left")
        ttk.Button(bottom, text="Head", command=self.head_current).pack(side="left", padx=(8,0))
        ttk.Button(bottom, text="Open in Preview", command=self.open_in_preview).pack(side="left", padx=(8,0))

        # defaults
        self.host.insert(0, "37.27.5.200")  # replace if needed
        self.port.insert(0, "8088")

    # ---- UI helpers ----

    def _show_text(self, text_str: str):
        self.img_label.pack_forget()
        if not self.text.winfo_ismapped():
            self.text.pack(fill="both", expand=True)
        self.text.delete("1.0", "end")
        self.text.insert("1.0", text_str)

    def _show_image_bytes(self, data: bytes):
        if not PIL_OK:
            self._show_text(f"<image {len(data)} bytes> (install Pillow for preview)")
            return
        try:
            img = Image.open(io.BytesIO(data)).convert("RGBA")
        except Exception as e:
            self._show_text(f"<failed to decode image: {e}>")
            return

        # scale to fit the right panel while preserving aspect
        right = self.text.master  # the 'right' frame
        right.update_idletasks()
        max_w = max(200, right.winfo_width() - 16)
        max_h = max(200, right.winfo_height() - 16)
        w, h = img.size
        scale = min(max_w / w, max_h / h, 1.0)
        if scale < 1.0:
            img = img.resize((int(w*scale), int(h*scale)), Image.LANCZOS)

        self.tk_img = ImageTk.PhotoImage(img)  # keep reference
        if self.text.winfo_ismapped():
            self.text.pack_forget()
        self.img_label.configure(image=self.tk_img)
        self.img_label.pack(fill="both", expand=True)

    # ---- actions ----

    def refresh(self):
        host, port = self.host.get().strip(), self.port.get().strip()
        try:
            entries = list_files(host, port)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        self.files.delete(0, "end")
        for name, mime, size in entries:
            if mime:
                label = f"{name}    [{mime}] ({size} bytes)"
            else:
                label = f"{name}    ({size} bytes)"
            self.files.insert("end", label)
        # clear preview
        self.current_name = None
        self.current_data = b""
        self.current_mime = ""
        self._show_text("")

    def _extract_name_from_list_label(self, label: str) -> str:
        # label formats: "name    (size bytes)" OR "name    [mime] (size bytes)"
        return label.split()[0]

    def on_select(self, _evt):
        sel = self.files.curselection()
        if not sel:
            return
        label = self.files.get(sel[0])
        name = self._extract_name_from_list_label(label)
        host, port = self.host.get().strip(), self.port.get().strip()
        try:
            data, mime, _size = fetch_file(host, port, name, head_only=False)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return

        self.current_name = name
        self.current_data = data
        self.current_mime = mime or self._guess_mime_from_name(name, data)

        if self.current_mime.startswith("image/"):
            self._show_image_bytes(data)
        else:
            try:
                self._show_text(data.decode("utf-8"))
            except UnicodeDecodeError:
                self._show_text(f"<binary {len(data)} bytes>")

    def head_current(self):
        sel = self.files.curselection()
        if not sel:
            return
        label = self.files.get(sel[0])
        name = self._extract_name_from_list_label(label)
        host, port = self.host.get().strip(), self.port.get().strip()
        try:
            _data, mime, size = fetch_file(host, port, name, head_only=True)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        messagebox.showinfo("HEAD", f"Name: {name}\nType: {mime or 'unknown'}\nSize: {size} bytes")

    def save_current(self):
        if not self.current_name:
            return
        path = filedialog.asksaveasfilename(initialfile=self.current_name)
        if not path:
            return
        try:
            with open(path, "wb") as f:
                f.write(self.current_data)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        messagebox.showinfo("Saved", f"Saved {len(self.current_data)} bytes to {path}")

    def on_open_preview(self, _evt=None):
        # double-click list item
        self.open_in_preview()

    def open_in_preview(self):
        if not self.current_name or not self.current_data:
            return
        suffix = os.path.splitext(self.current_name)[1] or ".bin"
        fd, path = tempfile.mkstemp(suffix=suffix)
        os.close(fd)
        try:
            with open(path, "wb") as f:
                f.write(self.current_data)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        try:
            # macOS Preview
            subprocess.run(["open", path], check=False)
        except Exception as e:
            messagebox.showerror("Error", str(e))

    # ---- utilities ----

    def _guess_mime_from_name(self, name: str, data: bytes) -> str:
        n = name.lower()
        if n.endswith(".png") or data[:8] == b"\x89PNG\r\n\x1a\n":
            return "image/png"
        if n.endswith(".jpg") or n.endswith(".jpeg") or data[:2] == b"\xff\xd8":
            return "image/jpeg"
        if n.endswith(".gif") or data[:6] in (b"GIF87a", b"GIF89a"):
            return "image/gif"
        if n.endswith(".bmp") or data[:2] == b"BM":
            return "image/bmp"
        if n.endswith(".webp") or (data[:4] == b"RIFF" and data[8:12] == b"WEBP"):
            return "image/webp"
        if n.endswith(".txt"):
            return "text/plain"
        if n.endswith(".md"):
            return "text/markdown"
        return ""

if __name__ == "__main__":
    App().mainloop()
