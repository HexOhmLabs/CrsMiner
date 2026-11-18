"""HexOhm CRSminer PC (Windows) — lightweight SHA-256d CPU miner desktop UI."""
from __future__ import annotations

import multiprocessing
import math
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import messagebox, ttk
from tkinter import font as tkfont
from pathlib import Path

from diagnostic_log import (clear_diagnostic_csv_files, diagnostic_csv_files,
                            diagnostics_directory)
from config_and_hardware import (default_settings, detect_windows_gpu, hardware_info,
                                 load_settings, save_settings, user_data_dir,
                                 validate_settings)
from fleet_api import LocalFleetServer
from miner_core import MiningEngine

PALETTES = {
    'dark': dict(bg='#0c111a', panel='#141e2c', tile='#1b2839', fg='#ecf2fa',
                 muted='#93a8be', border='#2c3c50', accent='#30c5ee',
                 highlight='#f4a340', green='#43d69d', danger='#f77886', entry='#101724', hover='#26384c'),
    'light': dict(bg='#f1f5fa', panel='#ffffff', tile='#f7faff', fg='#203047',
                  muted='#687f99', border='#d4deea', accent='#136ca6',
                  highlight='#c47813', green='#13885b', danger='#b93450', entry='#ffffff', hover='#e7eff9'),
}


def asset_path(name):
    base = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
    return str(base / 'assets' / name)


def human_rate(hps):
    for unit in ('H/s', 'kH/s', 'MH/s', 'GH/s', 'TH/s'):
        if hps < 1000 or unit == 'TH/s':
            return f'{hps:,.2f} {unit}'
        hps /= 1000


def _blend_hex(a, b, alpha):
    """Blend opaque CSS-style hexadecimal colours; UI-only helper."""
    alpha = max(0.0, min(1.0, alpha))
    x = tuple(int(a[k:k+2], 16) for k in (1,3,5))
    y = tuple(int(b[k:k+2], 16) for k in (1,3,5))
    return '#%02x%02x%02x' % tuple(round(u*(1-alpha)+v*alpha) for u,v in zip(x,y))


def _rounded_rect(canvas, x0, y0, x1, y1, radius, fill, outline):
    """A rounded Tk Canvas shape; no image library needed in the Windows EXE."""
    r = min(radius, (x1-x0)/2, (y1-y0)/2)
    # Explicit quarter-circle vertices keep straight edges truly straight.
    corners = ((x1-r,y0+r,270,360), (x1-r,y1-r,0,90),
               (x0+r,y1-r,90,180), (x0+r,y0+r,180,270))
    points=[]
    for cx,cy,start,end in corners:
        for k in range(9):
            theta=math.radians(start+(end-start)*k/8)
            points.extend((cx+r*math.cos(theta),cy+r*math.sin(theta)))
    return canvas.create_polygon(points, smooth=False,
                                 fill=fill, outline=outline, width=1)


class RoundedButton(tk.Canvas):
    """Accessible, keyboard-activatable rounded button drawn with Tk only."""
    def __init__(self, parent, text, command, background, foreground, palette, size=10, enabled=True):
        self._font = tkfont.Font(family='Segoe UI', size=size, weight='bold')
        width = max(55, self._font.measure(text)+29)
        height = max(37, self._font.metrics('linespace')+20)
        self._base = background
        self._fg = foreground
        self._palette = palette
        self._text = text
        self._command = command
        self._hover = False
        self._enabled = bool(enabled)
        super().__init__(parent, bg=parent.cget('bg'), width=width, height=height,
                         highlightthickness=0, bd=0, relief='flat',
                         cursor='hand2', takefocus=1)
        self.bind('<Configure>', self._paint)
        self.bind('<Enter>', self._enter)
        self.bind('<Leave>', self._leave)
        self.bind('<Button-1>', self._click)
        self.bind('<Return>', self._activate)
        self.bind('<space>', self._activate)
        self._paint()

    def set_enabled(self, enabled):
        self._enabled = bool(enabled)
        self.configure(cursor='hand2' if self._enabled else 'arrow')
        self._hover = False
        self._paint()

    def _enter(self, _event):
        self._hover = self._enabled
        self._paint()

    def _leave(self, _event):
        self._hover = False
        self._paint()

    def _click(self, _event):
        if not self._enabled:
            return
        self.focus_set()
        self._command()

    def _activate(self, _event):
        if self._enabled:
            self._command()

    def _paint(self, _event=None):
        self.delete('all')
        w, h = self.winfo_width(), self.winfo_height()
        if w < 30 or h < 20:
            w, h = int(self.cget('width')), int(self.cget('height'))
        background = (_blend_hex(self._base,self._palette['fg'],.13)
                      if self._hover and self._enabled else self._base)
        outline = _blend_hex(background,self._palette['fg'],.12)
        _rounded_rect(self,1,1,w-2,h-2,12,background,outline)
        text_color = self._fg if self._enabled else self._palette['muted']
        self.create_text(w/2,h/2,text=self._text,fill=text_color,
                         font=self._font,anchor='center')


class DesktopUI:
    def __init__(self, root):
        self.root = root
        self.settings, self.configured = load_settings()
        self.settings.setdefault('theme', 'dark')
        if self.settings['theme'] not in PALETTES:
            self.settings['theme'] = 'dark'
        self.colors = PALETTES[self.settings['theme']]
        self.last_page = 'Dashboard'
        self.draft = None
        self.form = {}
        self.status_vars = {}
        self.graph_history = []
        self.graph_canvas = None
        self.graph_latest_rate = 0.0
        self.graph_running = False
        self.graph_next_sample = 0.0
        self.log_queue = queue.Queue()
        self.log_history = []
        self.hardware = hardware_info()
        self.hardware_vars = {}
        self.engine = MiningEngine(on_log=self.log_queue.put)
        self.engine.config = dict(self.settings)
        self.fleet = LocalFleetServer(
            self.engine, lambda: dict(self.engine.config if self.engine.running else self.settings))
        self.root.title('CRSminer PC | HexOhm')
        self.root.geometry('1160x790')
        self.root.minsize(900, 680)
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.root.bind_all('<Control-Shift-q>', lambda _event: self.stop_mining())
        try:
            self.icon = tk.PhotoImage(file=asset_path('app_icon.png'))
            self.root.iconphoto(True, self.icon)
        except tk.TclError:
            self.icon = None
        self.load_wordmark()
        self.rebuild()
        self.root.after(500, self.tick)
        self.root.after(200, self.detect_gpu)
        if self.configured and self.settings.get('fleet', True):
            self.root.after(2100, self.update_fleet)

    def load_wordmark(self):
        self.wordmark = None
        self.splash_wordmark = None
        try:
            self.wordmark = tk.PhotoImage(file=asset_path('hexohm_wordmark.png')).subsample(7, 7)
            self.splash_wordmark = tk.PhotoImage(file=asset_path('hexohm_wordmark.png')).subsample(3, 3)
        except tk.TclError:
            pass

    def show_splash(self):
        splash = tk.Toplevel(self.root)
        splash.overrideredirect(True)
        splash.configure(bg='#080f1d')
        w, h = 580, 310
        x = (splash.winfo_screenwidth() - w) // 2
        y = (splash.winfo_screenheight() - h) // 2
        splash.geometry(f'{w}x{h}+{x}+{y}')
        splash.attributes('-topmost', True)
        canvas = tk.Canvas(splash, width=w, height=h, bg='#080f1d', highlightthickness=0)
        canvas.pack(fill='both', expand=True)
        canvas.create_rectangle(9, 9, w - 9, h - 9, outline='#1b3954', width=2)
        # Button-style beveled splash, using existing HexOhm wordmark.
        canvas.create_rectangle(34, 58, w - 34, 242, fill='#0b2036', outline='#1c91bf', width=3)
        canvas.create_line(40, 64, w - 40, 64, fill='#7be8f8', width=2)
        canvas.create_line(40, 240, w - 40, 240, fill='#082137', width=3)
        if self.splash_wordmark:
            canvas.create_image(w // 2, 132, image=self.splash_wordmark)
        else:
            canvas.create_text(w // 2, 133, text='HexOhm', fill='#e1effc', font=('Segoe UI', 40, 'bold'))
        canvas.create_text(w // 2, 207, text='CRSminer PC   •   SHA-256d CPU MINING',
                           fill='#89ddef', font=('Segoe UI', 11, 'bold'))
        canvas.create_text(w // 2, 273, text='Version 0.2.3  |  Windows',
                           fill='#9baebd', font=('Segoe UI', 10))

        def finish():
            if splash.winfo_exists():
                splash.destroy()
            self.root.deiconify()
            self.root.lift()
            self.open_page('Dashboard' if self.configured else 'Settings')
        splash.after(2000, finish)
        splash.update_idletasks()
        splash.lift()

    def theme(self):
        return self.colors

    def label(self, parent, text='', size=10, weight='normal', color=None, bg=None, **kw):
        c = self.colors
        return tk.Label(parent, text=text, fg=color or c['fg'], bg=bg or parent.cget('bg'),
                        font=('Segoe UI', size, weight), **kw)

    def button(self, parent, text, action, background=None, fg=None, size=10, **kw):
        c = self.colors
        return RoundedButton(parent, text, action, background or c['tile'],
                             fg or c['fg'], c, size=size)

    def frame(self, parent, bg=None, **kwargs):
        return tk.Frame(parent, bg=bg or self.colors['panel'], **kwargs)

    def rebuild(self):
        if hasattr(self, 'shell'):
            self.shell.destroy()
        c = self.colors
        self.root.configure(bg=c['bg'])
        self.shell = self.frame(self.root, bg=c['bg'])
        self.shell.pack(fill='both', expand=True)

        # Full-width workspace: navigation is only inside the Settings menu.
        header = self.frame(self.shell, bg=c['panel'], height=78)
        header.pack(fill='x')
        header.pack_propagate(False)
        if self.wordmark:
            tk.Label(header, image=self.wordmark, bg=c['panel']).pack(side='left', padx=(20, 13))
        else:
            self.label(header, 'HexOhm', 20, 'bold', color=c['accent']).pack(side='left', padx=20)
        brand = self.frame(header, bg=c['panel'])
        brand.pack(side='left')
        self.label(brand, 'CRSminer PC', 17, 'bold').pack(anchor='w')
        self.label(brand, 'SHA-256d  /  CPU MINING', 9, color=c['muted']).pack(anchor='w')
        controls = self.frame(header, bg=c['panel'])
        controls.pack(side='right', padx=18)
        self.settings_button = self.button(controls, '⚙  Settings  ▾', self.open_settings_menu,
                                           background=c['tile'])
        self.settings_button.pack(side='right', padx=(9, 0))
        self.button(controls, '☾ / ☀', self.toggle_theme, background=c['tile']).pack(side='right')
        self.label(controls, 'v0.2.3', 9, color=c['muted']).pack(side='right', padx=(0, 15))

        workspace = self.frame(self.shell, bg=c['bg'])
        workspace.pack(fill='both', expand=True, padx=18, pady=(15, 12))
        self.page_container = self.frame(workspace, bg=c['bg'])
        self.page_container.pack(fill='both', expand=True)
        self.render_page()
        footer = self.frame(self.shell, bg=c['panel'])
        footer.pack(fill='x')
        self.label(footer, '© HexOhm  ·  Local SHA-256d CPU mining  ·  Manual start', 9,
                   color=c['muted']).pack(side='left', padx=19, pady=7)
        self.label(footer, 'Fleet API  :8080', 9, color=c['muted']).pack(side='right', padx=19)

    def open_settings_menu(self):
        c = self.colors
        menu = tk.Menu(self.root, tearoff=0, bg=c['panel'], fg=c['fg'],
                       activebackground=c['hover'], activeforeground=c['accent'],
                       borderwidth=1, relief='flat', font=('Segoe UI', 10))
        for name, label in [('Dashboard', '▤  Mining dashboard'),
                            ('Settings', '⚙  Miner settings'),
                            ('Hardware', '▣  Computer hardware')]:
            menu.add_command(label=label, command=lambda p=name: self.open_page(p))
        menu.tk_popup(self.settings_button.winfo_rootx(),
                      self.settings_button.winfo_rooty() + self.settings_button.winfo_height() + 3)
        menu.grab_release()

    def metric_detail(self, key):
        """KPI tiles are real controls; show a live snapshot when clicked."""
        snap = self.engine.snapshot()
        c = self.colors
        titles = {'rate': 'Live CPU hashrate', 'accepted': 'Accepted shares',
                  'rejected': 'Rejected shares', 'up': 'Session uptime'}
        extras = {
            'rate': ('Total current hashrate', human_rate(snap['hr']),
                     'CPU configuration', f"{snap['threads']} logical CPUs · {snap.get('mining_mode', 'background').title()} mode"),
            'accepted': ('Pool-confirmed accepted shares', str(snap['acc']),
                         'Current share difficulty', f"{snap['pdiff']:.8g}"),
            'rejected': ('Pool-rejected shares', str(snap['rej']),
                         'Current connection state', str(snap['stage'])),
            'up': ('Time since miner started', self.status_vars['up'].get(),
                   'Current mining state', str(snap['stage'])),
        }
        item = extras.get(key)
        if not item:
            return
        dialog = tk.Toplevel(self.root)
        dialog.title(titles[key] + ' | CRSminer PC')
        dialog.configure(bg=c['panel'])
        dialog.resizable(False, False)
        dialog.transient(self.root)
        dialog.geometry('430x225')
        container = self.frame(dialog, bg=c['panel'])
        container.pack(fill='both', expand=True, padx=22, pady=18)
        self.label(container, titles[key], 16, 'bold').pack(anchor='w', pady=(0, 15))
        for label, value in ((item[0],item[1]),(item[2],item[3])):
            self.label(container, label, 9, color=c['muted']).pack(anchor='w')
            self.label(container, value, 13, 'bold', color=c['highlight']).pack(anchor='w', pady=(0, 12))
        self.button(container, 'Close', dialog.destroy).pack(side='bottom', anchor='e')

    def metric_card(self, parent, key, title, color, index):
        """Rounded, clickable statistics tile with unchanged data and behaviour."""
        c = self.colors
        card = tk.Canvas(parent, height=96, bg=c['bg'], bd=0,
                         highlightthickness=0, cursor='hand2', takefocus=1)
        card.grid(row=0, column=index, sticky='nsew',
                  padx=(0, 10) if index < 3 else 0)
        if not hasattr(self, 'metric_canvases'):
            self.metric_canvases = {}
        self.metric_canvases[key] = (card, title, color)
        card._hover = False
        card.bind('<Configure>', lambda _event, k=key: self.draw_metric_card(k))
        card.bind('<Enter>', lambda _event, k=key: self.set_metric_hover(k, True))
        card.bind('<Leave>', lambda _event, k=key: self.set_metric_hover(k, False))
        card.bind('<Button-1>', lambda _event, k=key: (card.focus_set(), self.metric_detail(k)))
        card.bind('<Return>', lambda _event, k=key: self.metric_detail(k))
        card.bind('<space>', lambda _event, k=key: self.metric_detail(k))
        return card

    def set_metric_hover(self, key, hovered):
        canvas = self.metric_canvases[key][0]
        canvas._hover = hovered
        self.draw_metric_card(key)

    def draw_metric_card(self, key):
        if key not in self.metric_canvases:
            return
        canvas, title, color = self.metric_canvases[key]
        if not canvas.winfo_exists():
            return
        c = self.colors
        w, h = canvas.winfo_width(), canvas.winfo_height()
        if w < 85 or h < 55:
            return
        canvas.delete('all')
        bg = c['hover'] if canvas._hover else c['panel']
        edge = _blend_hex(c['border'],color,.45 if canvas._hover else .20)
        _rounded_rect(canvas,1,1,w-2,h-2,16,bg,edge)
        # Subtle accent stripe; clipped away from the rounded corner.
        canvas.create_line(17,h-6,58,h-6,fill=_blend_hex(bg,color,.85),width=2)
        canvas.create_text(17,23,text=title,anchor='w',fill=c['muted'],
                           font=('Segoe UI',9,'bold'))
        canvas.create_text(w-19,23,text='↗',anchor='e',fill=color,
                           font=('Segoe UI',12,'bold'))
        canvas.create_text(17,61,text=self.status_vars[key].get(),anchor='w',
                           fill=color,font=('Segoe UI',20,'bold'))

    def toggle_theme(self):
        if self.last_page == 'Settings' and self.form:
            self.draft = self.read_form(validate=False)
        self.settings['theme'] = 'light' if self.settings['theme'] == 'dark' else 'dark'
        self.colors = PALETTES[self.settings['theme']]
        save_settings(self.settings)
        self.rebuild()

    def open_page(self, name):
        if name == self.last_page and self.page_container.winfo_children():
            return
        if self.last_page == 'Settings' and self.form:
            self.draft = self.read_form(validate=False)
        self.last_page = name
        self.rebuild()

    def render_page(self):
        for child in self.page_container.winfo_children():
            child.destroy()
        if self.last_page == 'Settings':
            self.render_settings()
        elif self.last_page == 'Hardware':
            self.render_hardware()
        else:
            self.render_dashboard()

    def page_heading(self, title, subtitle):
        c = self.colors
        self.label(self.page_container, title, 20, 'bold', bg=c['bg']).pack(anchor='w', pady=(2, 3))
        self.label(self.page_container, subtitle, 10, color=c['muted'], bg=c['bg']).pack(anchor='w', pady=(0, 15))

    def render_dashboard(self):
        c = self.colors
        top = self.frame(self.page_container, bg=c['bg'])
        top.pack(fill='x', pady=(0, 12))
        title = self.frame(top, bg=c['bg'])
        title.pack(side='left')
        self.label(title, 'Mining dashboard', 22, 'bold', bg=c['bg']).pack(anchor='w')
        self.label(title, 'Live SHA-256d  ·  verified pool shares  ·  local CPU', 10,
                   color=c['muted'], bg=c['bg']).pack(anchor='w', pady=(3, 0))
        actions = self.frame(top, bg=c['bg'])
        actions.pack(side='right')
        self.button(actions, '▶  START', self.start_mining, background=c['green'], fg='#071911').pack(side='left', padx=(0, 9))
        self.button(actions, '■  STOP', self.stop_mining, background=c['tile'], fg=c['danger']).pack(side='left')
        self.status_vars = {key: tk.StringVar(value='—') for key in
                            ('rate','accepted','rejected','up','stage','pool','best','diff','fleet','threads')}

        stats = self.frame(self.page_container, bg=c['bg'])
        stats.pack(fill='x', pady=(0, 12))
        self.metric_canvases = {}
        for i, (key, title, fg) in enumerate([
            ('rate', 'HASHRATE', c['highlight']),
            ('accepted', 'ACCEPTED SHARES', c['green']),
            ('rejected', 'REJECTED SHARES', c['danger']),
            ('up', 'SESSION UPTIME', c['accent'])]):
            stats.columnconfigure(i, weight=1, uniform='stat')
            self.metric_card(stats, key, title, fg, i)

        # Connection details are grouped in one compact full-width panel.
        details = self.frame(self.page_container, bg=c['panel'], highlightthickness=1,
                             highlightbackground=c['border'])
        details.pack(fill='x', pady=(0, 12))
        fields = [('stage', 'STATUS'), ('pool', 'POOL'), ('diff', 'POOL DIFFICULTY'),
                  ('best', 'BEST DIFFICULTY'), ('threads', 'CPU'), ('fleet', 'FLEET API')]
        for i,(key,heading) in enumerate(fields):
            row,col=divmod(i,3)
            cell=self.frame(details, bg=c['panel'])
            cell.grid(row=row,column=col,sticky='ew',padx=(16,8),pady=(11,6) if row==0 else (4,11))
            self.label(cell,heading,8,'bold',color=c['muted']).pack(anchor='w')
            self.label(cell,textvariable=self.status_vars[key],size=10,weight='bold').pack(anchor='w',pady=(5,0))
        for col in range(3): details.columnconfigure(col,weight=1,uniform='detail')

        graph_panel=self.frame(self.page_container,bg=c['panel'],highlightthickness=1,
                               highlightbackground=c['border'])
        graph_panel.pack(fill='x',pady=(0,12))
        graph_hdr=self.frame(graph_panel,bg=c['panel'])
        graph_hdr.pack(fill='x',padx=14,pady=(10,4))
        self.label(graph_hdr,'HASHRATE  /  LAST 5 MINUTES',10,'bold',color=c['muted']).pack(side='left')
        self.label(graph_hdr,'LIVE  •  2 SECOND SAMPLES',9,color=c['muted']).pack(side='right')
        self.graph_canvas=tk.Canvas(graph_panel,bg=c['entry'],height=138,
                                    highlightthickness=0,borderwidth=0)
        self.graph_canvas.pack(fill='x',padx=13,pady=(0,11))
        self.graph_canvas.bind('<Configure>',lambda _:self.draw_hashrate_graph())
        self.draw_hashrate_graph()

        log_title=self.frame(self.page_container,bg=c['bg'])
        log_title.pack(fill='x',pady=(0,6))
        self.label(log_title,'EVENT LOG',10,'bold',color=c['muted'],bg=c['bg']).pack(side='left')
        self.label(log_title,'POOL CONNECTIONS  /  SHARES  /  DIAGNOSTICS',8,
                   color=c['muted'],bg=c['bg']).pack(side='right')
        logarea=self.frame(self.page_container,bg=c['panel'],highlightthickness=1,
                           highlightbackground=c['border'])
        logarea.pack(fill='both',expand=True)
        self.log_widget=tk.Text(logarea,bg=c['entry'],fg=c['fg'],insertbackground=c['fg'],
                                borderwidth=0,font=('Consolas',10),wrap='word',height=8,
                                relief='flat',state='normal',padx=13,pady=11)
        scrollbar=tk.Scrollbar(logarea,command=self.log_widget.yview)
        self.log_widget.config(yscrollcommand=scrollbar.set)
        scrollbar.pack(side='right',fill='y')
        self.log_widget.pack(side='left',fill='both',expand=True)
        for line in self.log_history[-150:]:
            self.log_widget.insert('end',line+'\n')
        self.log_widget.configure(state='disabled')
        self.refresh_dashboard()

    def render_settings(self):
        c = self.colors
        self.page_heading('Miner settings', 'First-run setup and later changes. Mining starts only when you press Start.')
        cfg = dict(self.settings)
        if self.draft:
            cfg.update(self.draft)
            self.draft = None
        self.form = {}
        panel = self.frame(self.page_container, bg=c['panel'])
        panel.pack(fill='x', pady=(0, 10))
        panel.grid_columnconfigure(0, weight=1)
        panel.grid_columnconfigure(1, weight=1)

        fields = [
            ('name', 'Miner ID (4 letters/digits)', 'pc01'),
            ('worker', 'Worker name', 'pc'),
            ('pool', 'Stratum pool (host:port)', 'pool.example.org:3333'),
            ('wallet', 'Wallet / pool account (auto .worker)', 'Adds worker unless wallet already contains a dot'),
            ('password', 'Pool password (exact)', 'Enter your pool password; blank is allowed'),
            ('suggested_difficulty', 'Suggested share difficulty (optional)', 'blank = pool default'),
        ]
        for i, (key, heading, placeholder) in enumerate(fields):
            r, col = divmod(i, 2)
            item = self.frame(panel, bg=c['panel'])
            item.grid(row=r, column=col, sticky='ew', padx=14, pady=(12, 2))
            self.label(item, heading, 9, color=c['muted'], bg=c['panel']).pack(anchor='w', pady=(0, 5))
            var = tk.StringVar(value=str(cfg.get(key, '')))
            e = tk.Entry(item, textvariable=var, bg=c['entry'], fg=c['fg'],
                         insertbackground=c['fg'], relief='flat', bd=5,
                         highlightthickness=1, highlightbackground=c['border'],
                         font=('Segoe UI', 10), show='•' if key == 'password' else '')
            e.pack(fill='x')
            self.form[key] = var

        tools = self.frame(panel, bg=c['panel'])
        tools.grid(row=4, column=0, columnspan=2, sticky='ew', padx=14, pady=(9, 11))
        self.label(tools, 'Mining mode', 10, color=c['muted'], bg=c['panel']).pack(side='left')
        mode = ttk.Combobox(tools, values=['Background', 'Dedicated'], width=14, state='readonly')
        mode.set(str(cfg.get('mining_mode', 'background')).title())
        mode.pack(side='left', padx=(10, 26))
        self.form['mining_mode'] = mode
        fleet = tk.BooleanVar(value=bool(cfg.get('fleet', True)))
        tk.Checkbutton(tools, text='LAN fleet API :8080', variable=fleet,
                       bg=c['panel'], fg=c['fg'], selectcolor=c['entry'],
                       activebackground=c['panel'], activeforeground=c['fg'],
                       font=('Segoe UI', 10)).pack(side='left')
        self.form['fleet'] = fleet

        cpu_panel = self.frame(self.page_container, bg=c['panel'])
        cpu_panel.pack(fill='x', pady=(0, 9))
        self.label(cpu_panel, 'Logical CPU allocation — 0 to 100% effort', 11, 'bold',
                   bg=c['panel']).pack(anchor='w', padx=14, pady=(10, 2))
        self.label(cpu_panel, '0% disables that CPU. At least one CPU must be 75% or lower. '
                   'An effort cap limits mining, not Windows kernel activity.', 9,
                   color=c['muted'], bg=c['panel']).pack(anchor='w', padx=14, pady=(0, 6))
        list_wrap = self.frame(cpu_panel, bg=c['panel'])
        list_wrap.pack(fill='x', padx=14, pady=(0, 9))
        cpu_total = os.cpu_count() or 1
        cpu_rows = (cpu_total + 1) // 2
        cpu_canvas = tk.Canvas(list_wrap, bg=c['panel'], highlightthickness=0, borderwidth=0,
                               height=min(136, max(36, 34 * cpu_rows)))
        cpu_canvas.pack(side='left', fill='x', expand=True)
        cpu_scroll = ttk.Scrollbar(list_wrap, orient='vertical', command=cpu_canvas.yview)
        if cpu_rows > 4:
            cpu_scroll.pack(side='right', fill='y')
        cpu_canvas.configure(yscrollcommand=cpu_scroll.set)
        cpu_content = self.frame(cpu_canvas, bg=c['panel'])
        window_id = cpu_canvas.create_window((0, 0), window=cpu_content, anchor='nw')
        cpu_content.bind('<Configure>', lambda event:
                         cpu_canvas.configure(scrollregion=cpu_canvas.bbox('all')))
        cpu_canvas.bind('<Configure>', lambda event:
                        cpu_canvas.itemconfigure(window_id, width=event.width))
        efforts = cfg.get('cpu_efforts', [])
        cpu_vars = []
        for cpu in range(cpu_total):
            row, side = divmod(cpu, 2)
            cell = self.frame(cpu_content, bg=c['panel'])
            cell.grid(row=row, column=side, sticky='w', padx=(0, 32), pady=3)
            self.label(cell, f'Logical CPU {cpu}', 10, bg=c['panel']).pack(side='left', padx=(0, 8))
            value = efforts[cpu] if cpu < len(efforts) else 0
            var = tk.StringVar(value=str(value))
            spin = tk.Spinbox(cell, from_=0, to=100, increment=1, width=5,
                              textvariable=var, font=('Segoe UI', 10),
                              bg=c['entry'], fg=c['fg'], buttonbackground=c['tile'],
                              insertbackground=c['fg'], relief='flat')
            spin.pack(side='left')
            self.label(cell, '%', 9, color=c['muted'], bg=c['panel']).pack(side='left', padx=5)
            cpu_vars.append(var)
        self.form['cpu_efforts'] = cpu_vars
        self.label(cpu_panel, 'Dedicated: pin selected workers, normal priority.  '
                   'Background: pinned workers, below-normal priority.  '
                   'Stop shortcut: Ctrl+Shift+Q.', 9,
                   color=c['muted'], bg=c['panel']).pack(anchor='w', padx=14, pady=(0, 8))

        # Optional CSV diagnostics; does not change mining scheduling or engine.
        diagnostics_row = self.frame(self.page_container, bg=c['panel'])
        diagnostics_row.pack(fill='x', pady=(0, 5))
        diagnostic_switch = tk.BooleanVar(value=bool(cfg.get('diagnostics_enabled', True)))
        toggle = tk.Checkbutton(diagnostics_row, text='Diagnostic CSV logging',
                                variable=diagnostic_switch,
                                bg=c['panel'], fg=c['fg'], selectcolor=c['entry'],
                                activebackground=c['panel'], activeforeground=c['fg'],
                                font=('Segoe UI', 10, 'bold'))
        toggle.pack(side='left', padx=(14, 12), pady=7)
        self.form['diagnostics_enabled'] = diagnostic_switch
        folder_btn = RoundedButton(diagnostics_row, 'Open diagnostics folder',
                                   self.open_diagnostics_folder, c['tile'], c['fg'], c,
                                   size=9, enabled=diagnostic_switch.get())
        folder_btn.pack(side='left', padx=(0, 10), pady=4)
        self.button(diagnostics_row, 'Clear diagnostic files',
                    self.clear_diagnostic_files, size=9).pack(side='left', pady=4)
        diagnostic_switch.trace_add('write',
                                     lambda *_: folder_btn.set_enabled(diagnostic_switch.get()))

        # Use the same compact row for Save buttons to retain the original
        # settings-page fit at 900x680 and avoid introducing a scrollbar.
        actions = self.frame(diagnostics_row, bg=c['panel'])
        actions.pack(side='right', padx=(5, 10))
        self.button(actions, 'Save settings', self.save_clicked, background=c['accent'], fg='#06131c', size=9).pack(side='left')
        self.button(actions, 'Save and open Dashboard', lambda: self.save_clicked(show_dashboard=True), size=9).pack(side='left', padx=(7, 0))
        helptext = ('CPU changes apply after Stop and Start. Pool login and password are sent exactly as typed. '
                    'The optional LAN API can reveal account details; never expose port 8080 to the Internet. '
                    'Settings use config_v023.json (password stored as plaintext).')
        self.label(self.page_container, helptext, 10, color=c['muted'],
                   bg=c['bg'], justify='left', wraplength=1060).pack(anchor='w', pady=(8, 0))

    def read_form(self, validate=True):
        cfg = dict(self.settings)
        for key in ('name', 'pool', 'worker', 'suggested_difficulty'):
            cfg[key] = self.form[key].get().strip()
        # Keep the wallet/account as entered; miner_core derives the pool worker suffix only when needed.
        # Do not trim, alter, or construct either one.
        cfg['wallet'] = self.form['wallet'].get()
        cfg['password'] = self.form['password'].get()
        try:
            efforts = [int(value.get()) for value in self.form['cpu_efforts']]
            cfg['cpu_efforts'] = efforts
            cfg['mining_mode'] = self.form['mining_mode'].get().lower()
            active = [value for value in efforts if value > 0]
            cfg['threads'] = len(active)
            cfg['effort'] = round(sum(active) / len(active)) if active else 0
        except (ValueError, KeyError):
            if validate:
                raise ValueError('Enter whole-number effort percentages (0–100) for each CPU.')
        cfg['fleet'] = bool(self.form['fleet'].get())
        cfg['diagnostics_enabled'] = bool(self.form['diagnostics_enabled'].get())
        if validate:
            validate_settings(cfg)
        return cfg

    def open_diagnostics_folder(self):
        # The UI control is disabled immediately when the toggle is switched off.
        if self.form and not self.form['diagnostics_enabled'].get():
            return
        directory = diagnostics_directory(create=True)
        try:
            if os.name != 'nt':
                raise OSError('Windows Explorer is required to open this folder.')
            os.startfile(str(directory))
        except OSError as exc:
            messagebox.showerror('Cannot open diagnostics folder', str(exc),
                                 parent=self.root)

    def clear_diagnostic_files(self):
        # Never remove an open/live diagnostic CSV or stop workers implicitly.
        if self.engine.running:
            messagebox.showinfo('Stop mining first',
                                'Stop mining before clearing diagnostic files.',
                                parent=self.root)
            return
        try:
            files = diagnostic_csv_files()
        except OSError as exc:
            messagebox.showerror('Cannot list diagnostics', str(exc), parent=self.root)
            return
        if not files:
            messagebox.showinfo('Diagnostics', 'No CRSminer diagnostic CSV files found.',
                                parent=self.root)
            return
        if not messagebox.askyesno('Delete diagnostic CSV files?',
                f'Permanently delete {len(files)} CRSminer diagnostic CSV file(s)?\n\n'
                'This cannot be undone. Other files and settings are preserved.',
                parent=self.root):
            return
        try:
            deleted, failures = clear_diagnostic_csv_files()
        except OSError as exc:
            messagebox.showerror('Diagnostics cleanup failed', str(exc), parent=self.root)
            return
        if failures:
            messagebox.showwarning('Diagnostics cleanup',
                f'Deleted {deleted} file(s); could not remove {len(failures)} file(s).\n'
                + '\n'.join(name for name, _ in failures[:5]), parent=self.root)
        else:
            messagebox.showinfo('Diagnostics cleanup',
                                f'Deleted {deleted} diagnostic CSV file(s).', parent=self.root)

    def save_clicked(self, show_dashboard=False):
        try:
            cfg = self.read_form()
        except (ValueError, KeyError) as exc:
            messagebox.showerror('Invalid settings', str(exc), parent=self.root)
            return
        if self.engine.running:
            messagebox.showinfo('Mining still active',
                                'Your new pool and CPU settings will take effect after Stop and Start.',
                                parent=self.root)
        self.settings = cfg
        self.engine.config = dict(cfg) if not self.engine.running else self.engine.config
        self.configured = True
        self.draft = None
        save_settings(cfg)
        self.update_fleet()
        self.log_queue.put('Settings saved to ' + str(user_data_dir()))
        if show_dashboard:
            self.open_page('Dashboard')
        else:
            messagebox.showinfo('Saved', 'Settings saved. Use Dashboard → Start mining.', parent=self.root)

    def update_fleet(self):
        if self.settings.get('fleet') and self.configured:
            self.fleet.start()
            if self.fleet.error:
                self.log_queue.put(self.fleet.error)
        else:
            self.fleet.stop()

    def render_hardware(self):
        c = self.colors
        self.page_heading('Detected computer hardware', 'CPU mining only; GPU detection is informational.')
        box = self.frame(self.page_container, bg=c['panel'])
        box.pack(fill='x')
        names = [('cpu', 'Processor'), ('cpus', 'Logical CPU threads'), ('clock', 'Nominal clock'),
                 ('ram', 'Installed RAM'), ('gpu', 'Display adapter (not used for mining)'),
                 ('architecture', 'Architecture'), ('os', 'Operating system'), ('hostname', 'Computer name')]
        self.hardware_vars = {}
        for i, (key, label) in enumerate(names):
            self.label(box, label, 10, color=c['muted'], bg=c['panel']).grid(row=i, column=0, sticky='nw', padx=17, pady=11)
            var = tk.StringVar(value=str(self.hardware.get(key, '—')))
            self.hardware_vars[key] = var
            self.label(box, textvariable=var, size=10, weight='bold',
                       bg=c['panel'], justify='left', wraplength=480).grid(row=i, column=1, sticky='w', padx=16, pady=11)
        box.columnconfigure(1, weight=1)
        self.label(self.page_container,
                   'CPU threads and RAM are detected at launch. GPU identification may take a few seconds. '
                   'No GPU mining, automatic overclocking or system modification is performed.',
                   10, color=c['muted'], bg=c['bg'], justify='left', wraplength=720).pack(anchor='w', pady=18)

    def detect_gpu(self):
        def detect():
            gpu = detect_windows_gpu()
            self.hardware['gpu'] = gpu
            self.root.after(0, lambda: self.hardware_vars['gpu'].set(gpu) if 'gpu' in self.hardware_vars else None)
        threading.Thread(target=detect, daemon=True).start()

    def refresh_dashboard(self):
        if not self.status_vars:
            return
        state = self.engine.snapshot()
        h, secs = divmod(state['up'], 3600)
        m, s = divmod(secs, 60)
        values = dict(rate=human_rate(state['hr']), accepted=str(state['acc']),
                      rejected=str(state['rej']), up=f'{h:02d}:{m:02d}:{s:02d}',
                      stage=state['stage'][:58], pool=state['pool'][:42] or 'Not configured',
                      diff=f"{state['pdiff']:.6g}", best=f"{state['best']:.6g}",
                      threads=(f"{state['threads']} logical CPUs · {state.get('mining_mode', 'background').title()}"
                               if state['running'] else
                               f"Ready: {sum(1 for x in self.settings.get('cpu_efforts', []) if x > 0)} CPUs · "
                               f"{self.settings.get('mining_mode', 'background').title()}"),
                      fleet=('Not configured' if not self.configured else
                             'Disabled' if not self.settings.get('fleet') else
                             'Port 8080 unavailable' if self.fleet.error else
                             'Port 8080 • LAN' if self.fleet.httpd else 'Starting…'))
        for key, value in values.items():
            self.status_vars[key].set(value)
        if hasattr(self, 'metric_canvases'):
            for key in ('rate', 'accepted', 'rejected', 'up'):
                self.draw_metric_card(key)

    def draw_hashrate_graph(self):
        """ESP-style scrolling hashrate trace with 5-minute history.

        All Tk canvas operations run on the UI thread. Values come from
        MiningEngine.snapshot(), never from synthetic graph data.
        """
        canvas = self.graph_canvas
        if not canvas or not canvas.winfo_exists():
            return
        c = self.colors
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 80 or h < 50:
            return
        canvas.delete('all')
        left, top, right, bottom = 91, 12, w - 12, h - 23
        plotw, ploth = max(10, right-left), max(10, bottom-top)
        hist = self.graph_history[-150:]
        upper = max([1.0, *(v for v in hist)]) * 1.15
        # Horizontal grid: 0/25/50/75/100%; time ticks every 60s.
        for i in range(5):
            y = bottom - ploth*i/4
            grid_color = _blend_hex(c['border'],c['accent'],.16 if i in (0,4) else .06)
            canvas.create_line(left, y, right, y, fill=grid_color)
            canvas.create_text(left-6, y, text=human_rate(upper*i/4),
                               fill=c['muted'], font=('Consolas', 8), anchor='e')
        for k in range(6):
            x = left + k*plotw/5
            canvas.create_line(x, top, x, bottom, fill=_blend_hex(c['border'],c['accent'],.10), dash=(2, 4))
            canvas.create_text(x, bottom+12, text=f'-{5-k}m' if k<5 else 'now',
                               fill=c['muted'], font=('Consolas', 8))
        if len(hist) >= 2:
            # Same positions, samples, dimensions and axes as v0.1.4.
            coords = []
            for i, v in enumerate(hist):
                x = right - (len(hist)-1-i)*plotw/149
                y = bottom - min(upper, max(0, v))*ploth/upper
                coords.append((x,y))
            stops = (('#24cde6','#7a82ff','#b175ed','#ffb04a') if self.settings['theme']=='dark'
                     else ('#087a9d','#5d66d3','#8e4aa6','#c7760c'))
            def spectrum(frac):
                t = min(2.999,max(0.0,frac*3))
                i = int(t)
                return _blend_hex(stops[i],stops[i+1],t-i)
            n = len(coords)-1
            for i in range(n):
                (x0,y0),(x1,y1) = coords[i],coords[i+1]
                shade = spectrum(i/max(1,n))
                # Light coloured under-trace accents, no change to graph geometry.
                canvas.create_line(x1,y1,x1,bottom,fill=_blend_hex(c['entry'],shade,.13),width=2)
            for i in range(n):
                (x0,y0),(x1,y1) = coords[i],coords[i+1]
                shade = spectrum(i/max(1,n))
                canvas.create_line(x0,y0,x1,y1,fill=_blend_hex(c['entry'],shade,.40),width=6)
                canvas.create_line(x0,y0,x1,y1,fill=shade,width=2.5)
            x,y = coords[-1]
            canvas.create_oval(x-4,y-4,x+4,y+4,fill=stops[-1],outline=c['entry'],width=1)
        if hist:
            canvas.create_text(right-2, top+2, text=human_rate(hist[-1]),
                               fill=c['highlight'], font=('Segoe UI', 9, 'bold'), anchor='ne')

    def tick(self):
        try:
            while True:
                msg = self.log_queue.get_nowait()
                self.log_history.append(msg)
                if len(self.log_history) > 300:
                    self.log_history = self.log_history[-300:]
                if hasattr(self, 'log_widget') and self.log_widget.winfo_exists():
                    self.log_widget.configure(state='normal')
                    self.log_widget.insert('end', msg + '\n')
                    self.log_widget.see('end')
                    self.log_widget.configure(state='disabled')
        except queue.Empty:
            pass
        import time
        now = time.monotonic()
        if now >= self.graph_next_sample:
            snap = self.engine.snapshot()
            running = bool(snap['running'])
            if running and not self.graph_running:
                self.graph_history.clear()
            self.graph_running = running
            self.graph_history.append(float(snap['hr']) if running else 0.0)
            if len(self.graph_history) > 150:
                self.graph_history = self.graph_history[-150:]
            self.graph_next_sample = now + 2.0
            self.draw_hashrate_graph()
        if self.last_page == 'Dashboard':
            self.refresh_dashboard()
        self.root.after(500, self.tick)

    def start_mining(self):
        if self.engine.running:
            messagebox.showinfo('Mining', 'Mining is already running.', parent=self.root)
            return
        try:
            validate_settings(self.settings)
            self.engine.start(self.settings)
        except (ValueError, OSError, RuntimeError) as exc:
            messagebox.showerror('Cannot start mining', str(exc), parent=self.root)
            self.open_page('Settings')

    def stop_mining(self):
        if self.engine.running:
            self.engine.stop()

    def close(self):
        self.engine.stop()
        self.fleet.stop()
        self.root.destroy()


def main():
    multiprocessing.freeze_support()
    root = tk.Tk()
    root.withdraw()
    ui = DesktopUI(root)
    ui.show_splash()
    root.mainloop()


if __name__ == '__main__':
    main()
