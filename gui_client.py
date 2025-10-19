import socket, tkinter as tk
from tkinter import ttk, messagebox

def list_files(host, port):
    with socket.create_connection((host, int(port)), timeout=5) as s:
        s.sendall(b"LIST\n")
        # Read header "FILES n\n"
        head = b""
        while not head.endswith(b"\n"):
            ch = s.recv(1)
            if not ch: break
            head += ch
        if not head.startswith(b"FILES "):
            raise RuntimeError("Bad LIST header: %r" % head)
        # Read until blank line
        lines = b""
        while True:
            ch = s.recv(1)
            if not ch: break
            lines += ch
            if lines.endswith(b"\n\n"):
                lines = lines[:-2]
                break
        entries = []
        for ln in lines.decode("utf-8", "replace").splitlines():
            if not ln.strip(): continue
            # "<name>\t<size>"
            if "\t" in ln:
                name, size = ln.split("\t", 1)
                entries.append((name, size))
        return entries

def fetch_file(host, port, name, head_only=False):
    with socket.create_connection((host, int(port)), timeout=5) as s:
        cmd = ("HEAD " if head_only else "GET ") + name + "\n"
        s.sendall(cmd.encode("utf-8"))
        # Read "SIZE n\n"
        line = b""
        while not line.endswith(b"\n"):
            ch = s.recv(1)
            if not ch: break
            line += ch
        if not line.startswith(b"SIZE "):
            raise RuntimeError("Bad SIZE header: %r" % line)
        n = int(line.split()[1])
        # Blank line
        blank = b""
        while not blank.endswith(b"\n"):
            ch = s.recv(1)
            if not ch: break
            blank += ch
        if head_only: return b""
        # Body
        data = bytearray()
        left = n
        while left > 0:
            chunk = s.recv(min(65536, left))
            if not chunk: break
            data.extend(chunk)
            left -= len(chunk)
        return bytes(data)

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Tiny File Client")
        self.geometry("700x500")

        frm = ttk.Frame(self, padding=8); frm.pack(fill="both", expand=True)
        top = ttk.Frame(frm); top.pack(fill="x", pady=(0,8))

        ttk.Label(top, text="Host:").pack(side="left")
        self.host = ttk.Entry(top, width=24); self.host.pack(side="left", padx=(4,10))
        ttk.Label(top, text="Port:").pack(side="left")
        self.port = ttk.Entry(top, width=7); self.port.pack(side="left", padx=(4,10))
        ttk.Button(top, text="Connect / Refresh", command=self.refresh).pack(side="left")

        mid = ttk.Frame(frm); mid.pack(fill="both", expand=True)
        self.files = tk.Listbox(mid, width=30)
        self.files.pack(side="left", fill="y")
        self.files.bind("<<ListboxSelect>>", self.on_select)

        self.text = tk.Text(mid, wrap="word")
        self.text.pack(side="left", fill="both", expand=True, padx=(8,0))

        # Quick defaults (replace with your VM IP/port)
        self.host.insert(0, "37.27.5.200")
        self.port.insert(0, "8088")

        bot = ttk.Frame(frm); bot.pack(fill="x", pady=(8,0))
        ttk.Button(bot, text="Save to file…", command=self.save_current).pack(side="left")

    def refresh(self):
        host, port = self.host.get().strip(), self.port.get().strip()
        try:
            entries = list_files(host, port)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        self.files.delete(0, "end")
        for name, size in entries:
            self.files.insert("end", f"{name}    ({size} bytes)")

    def on_select(self, _evt):
        sel = self.files.curselection()
        if not sel: return
        label = self.files.get(sel[0])
        name = label.split()[0]
        host, port = self.host.get().strip(), self.port.get().strip()
        try:
            data = fetch_file(host, port, name, head_only=False)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        self.text.delete("1.0", "end")
        try:
            self.text.insert("1.0", data.decode("utf-8"))
        except UnicodeDecodeError:
            self.text.insert("1.0", f"<binary {len(data)} bytes>\n")

    def save_current(self):
        sel = self.files.curselection()
        if not sel: return
        label = self.files.get(sel[0]); name = label.split()[0]
        host, port = self.host.get().strip(), self.port.get().strip()
        try:
            data = fetch_file(host, port, name, head_only=False)
        except Exception as e:
            messagebox.showerror("Error", str(e)); return
        from tkinter.filedialog import asksaveasfilename
        path = asksaveasfilename(initialfile=name)
        if not path: return
        with open(path, "wb") as f: f.write(data)
        messagebox.showinfo("Saved", f"Saved {len(data)} bytes to {path}")

if __name__ == "__main__":
    App().mainloop()
