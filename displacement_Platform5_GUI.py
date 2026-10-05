import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time


BAUD_RATE = 115200
MOTOR_COUNT = 4


class StepperGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Displacement_Platform4_GUI")
        self.root.geometry("640x620")
        self.root.minsize(640, 620)
        self.root.maxsize(640, 620)

        self.ser = None
        self.serial_thread = None
        self.serial_running = False

        # 防止不同執行緒同時寫入 Serial
        self.serial_write_lock = threading.Lock()

        # GUI 更新佇列
        self.ui_queue = queue.Queue()
        self.move_done_event = threading.Event()

        # Loop Cycle
        self.loop_thread = None
        self.loop_stop_event = threading.Event()

        self.create_widgets()
        self.refresh_ports()

        self.root.after(100, self.process_ui_queue)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def create_widgets(self):
        # =========================================================
        # Top Area: Serial Connection + Emergency Stop
        # =========================================================
        top_frame = ttk.Frame(self.root)
        top_frame.pack(fill="x", padx=10, pady=(8, 4))
        top_frame.columnconfigure(0, weight=1)
        top_frame.columnconfigure(1, weight=0)

        # ---------------- Serial Connection ----------------
        conn_frame = ttk.LabelFrame(top_frame, text="Serial Connection")
        conn_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8))

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, padx=5, pady=8)

        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            conn_frame,
            textvariable=self.port_var,
            width=20,
            state="readonly",
        )
        self.port_combo.grid(row=0, column=1, padx=5, pady=8)

        self.refresh_btn = ttk.Button(
            conn_frame,
            text="Refresh",
            command=self.refresh_ports,
        )
        self.refresh_btn.grid(row=0, column=2, padx=5, pady=8)

        self.connect_btn = ttk.Button(
            conn_frame,
            text="Connect",
            command=self.toggle_connection,
        )
        self.connect_btn.grid(row=0, column=3, padx=5, pady=8)

        self.status_label = ttk.Label(
            conn_frame,
            text="Disconnected",
            foreground="red",
        )
        self.status_label.grid(row=0, column=4, padx=10, pady=8)

        # ---------------- Emergency Stop ----------------
        stop_frame = ttk.LabelFrame(top_frame, text="Emergency Stop")
        stop_frame.grid(row=0, column=1, sticky="ns")

        self.stop_btn = tk.Button(
            stop_frame,
            text="●  STOP",
            command=self.send_emergency_stop,
            bg="#d32f2f",
            fg="white",
            activebackground="#b71c1c",
            activeforeground="white",
            font=("Segoe UI", 11, "bold"),
            relief="raised",
            bd=2,
            padx=18, 
            pady=8,
            cursor="hand2",
        )
        self.stop_btn.pack(padx=14, pady=10)

        # =========================================================
        # Manual Motor Control
        # =========================================================
        manual_frame = ttk.LabelFrame(self.root, text="Manual Motor Control")
        manual_frame.pack(fill="x", padx=10, pady=6)

        # Column headers
        ttk.Label(manual_frame, text="Motor").grid(row=0, column=0, padx=8, pady=(8, 4))
        ttk.Label(manual_frame, text="Steps").grid(row=0, column=1, padx=8, pady=(8, 4))
        ttk.Label(manual_frame, text="Delay (us)").grid(row=0, column=2, padx=8, pady=(8, 4))
        ttk.Label(manual_frame, text="Ramp").grid(row=0, column=3, padx=8, pady=(8, 4))
        ttk.Label(manual_frame, text="Direction").grid(
            row=0,
            column=4,
            columnspan=2,
            padx=8,
            pady=(8, 4),
        )

        # Each motor gets independent parameters
        self.manual_steps_vars = []
        self.manual_delay_vars = []
        self.manual_ramp_vars = []

        for motor in range(MOTOR_COUNT):
            row = motor + 1

            steps_var = tk.StringVar(value="1")
            delay_var = tk.StringVar(value="3")
            ramp_var = tk.StringVar(value="1")

            self.manual_steps_vars.append(steps_var)
            self.manual_delay_vars.append(delay_var)
            self.manual_ramp_vars.append(ramp_var)

            ttk.Label(manual_frame, text=f"Motor {motor}").grid(
                row=row,
                column=0,
                padx=8,
                pady=5,
                sticky="w",
            )

            ttk.Entry(manual_frame, textvariable=steps_var, width=14).grid(
                row=row,
                column=1,
                padx=8,
                pady=5,
            )

            ttk.Entry(manual_frame, textvariable=delay_var, width=14).grid(
                row=row,
                column=2,
                padx=8,
                pady=5,
            )

            ttk.Entry(manual_frame, textvariable=ramp_var, width=14).grid(
                row=row,
                column=3,
                padx=8,
                pady=5,
            )

            ttk.Button(
                manual_frame,
                text="Dir 0",
                command=lambda m=motor: self.send_manual_move(m, 0),
                width=12,
            ).grid(row=row, column=4, padx=(8, 4), pady=5)

            ttk.Button(
                manual_frame,
                text="Dir 1",
                command=lambda m=motor: self.send_manual_move(m, 1),
                width=12,
            ).grid(row=row, column=5, padx=(4, 8), pady=5)

        for col in range(6):
            manual_frame.columnconfigure(col, weight=1 if col in (1, 2, 3) else 0)

        # =========================================================
        # Loop Cycle
        # =========================================================
        loop_frame = ttk.LabelFrame(self.root, text="Loop Cycle")
        loop_frame.pack(fill="x", padx=10, pady=6)

        # First row: primary motion parameters
        ttk.Label(loop_frame, text="Motor").grid(row=0, column=0, padx=6, pady=(8, 4))
        ttk.Label(loop_frame, text="Steps").grid(row=0, column=1, padx=6, pady=(8, 4))
        ttk.Label(loop_frame, text="Delay (us)").grid(row=0, column=2, padx=6, pady=(8, 4))
        ttk.Label(loop_frame, text="Ramp").grid(row=0, column=3, padx=6, pady=(8, 4))
        ttk.Label(loop_frame, text="Cycles").grid(row=0, column=4, padx=6, pady=(8, 4))
        ttk.Label(loop_frame, text="Start Direction").grid(row=0, column=5, padx=6, pady=(8, 4))

        self.loop_motor_var = tk.StringVar(value="0")
        self.loop_steps_var = tk.StringVar(value="1")
        self.loop_delay_us_var = tk.StringVar(value="3")
        self.loop_ramp_var = tk.StringVar(value="1")
        self.loop_cycles_var = tk.StringVar(value="10")
        self.loop_start_dir_var = tk.StringVar(value="Dir 0")

        self.loop_motor_combo = ttk.Combobox(
            loop_frame,
            textvariable=self.loop_motor_var,
            values=[str(i) for i in range(MOTOR_COUNT)],
            width=10,
            state="readonly",
        )
        self.loop_motor_combo.grid(row=1, column=0, padx=6, pady=(2, 8))

        ttk.Entry(loop_frame, textvariable=self.loop_steps_var, width=12).grid(
            row=1,
            column=1,
            padx=6,
            pady=(2, 8),
        )

        ttk.Entry(loop_frame, textvariable=self.loop_delay_us_var, width=12).grid(
            row=1,
            column=2,
            padx=6,
            pady=(2, 8),
        )

        ttk.Entry(loop_frame, textvariable=self.loop_ramp_var, width=12).grid(
            row=1,
            column=3,
            padx=6,
            pady=(2, 8),
        )

        ttk.Entry(loop_frame, textvariable=self.loop_cycles_var, width=12).grid(
            row=1,
            column=4,
            padx=6,
            pady=(2, 8),
        )

        self.loop_start_dir_combo = ttk.Combobox(
            loop_frame,
            textvariable=self.loop_start_dir_var,
            values=["Dir 0", "Dir 1"],
            width=12,
            state="readonly",
        )
        self.loop_start_dir_combo.grid(row=1, column=5, padx=6, pady=(2, 8))

        # Second row: loop timing + start
        ttk.Label(loop_frame, text="Gap between directions (s)").grid(
            row=2,
            column=0,
            columnspan=2,
            padx=6,
            pady=(6, 3),
            sticky="w",
        )
        ttk.Label(loop_frame, text="Gap between cycles (s)").grid(
            row=2,
            column=2,
            columnspan=2,
            padx=6,
            pady=(6, 3),
            sticky="w",
        )

        self.loop_gap_dir_var = tk.StringVar(value="0.5")
        self.loop_gap_cycle_var = tk.StringVar(value="1.0")

        ttk.Entry(loop_frame, textvariable=self.loop_gap_dir_var, width=16).grid(
            row=3,
            column=0,
            columnspan=2,
            padx=6,
            pady=(2, 8),
            sticky="w",
        )

        ttk.Entry(loop_frame, textvariable=self.loop_gap_cycle_var, width=16).grid(
            row=3,
            column=2,
            columnspan=2,
            padx=6,
            pady=(2, 8),
            sticky="w",
        )

        self.start_loop_btn = ttk.Button(
            loop_frame,
            text="Start Loop",
            command=self.start_loop_cycle,
        )
        self.start_loop_btn.grid(
            row=2,
            column=4,
            columnspan=2,
            rowspan=2,
            padx=8,
            pady=8,
            sticky="nsew",
        )

        for col in range(6):
            loop_frame.columnconfigure(col, weight=1)

        # =========================================================
        # Serial Monitor
        # =========================================================
        monitor_frame = ttk.LabelFrame(self.root, text="Serial Monitor")
        monitor_frame.pack(fill="both", expand=False, padx=10, pady=6)

        text_container = ttk.Frame(monitor_frame)
        text_container.pack(fill="both", expand=True, padx=5, pady=5)

        self.monitor = tk.Text(
            text_container,
            height=5,
            wrap="word",
            font=("Consolas", 10),
        )
        self.monitor.pack(side="left", fill="both", expand=True)

        scrollbar = ttk.Scrollbar(text_container, command=self.monitor.yview)
        scrollbar.pack(side="right", fill="y")
        self.monitor.config(yscrollcommand=scrollbar.set)

        # Different source colors in the Serial Monitor
        self.monitor.tag_configure("PC", foreground="#1565c0")
        self.monitor.tag_configure("Arduino", foreground="#2e7d32")
        self.monitor.tag_configure("Loop", foreground="#6a1b9a")
        self.monitor.tag_configure("Emergency", foreground="#d32f2f", font=("Consolas", 10, "bold"))
        self.monitor.tag_configure("Error", foreground="#c62828", font=("Consolas", 10, "bold"))
        self.monitor.tag_configure("Info", foreground="#455a64")

        bottom_frame = ttk.Frame(monitor_frame)
        bottom_frame.pack(fill="x", padx=5, pady=(0, 5))

        ttk.Button(
            bottom_frame,
            text="Clear Monitor",
            command=self.clear_monitor,
        ).pack(side="left")

    # =============================================================
    # Serial Connection
    # =============================================================
    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [port.device for port in ports]
        self.port_combo["values"] = port_list

        if port_list:
            self.port_var.set(port_list[0])
        else:
            self.port_var.set("")

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.disconnect_serial()
        else:
            self.connect_serial()

    def connect_serial(self):
        port = self.port_var.get()

        if not port:
            messagebox.showerror("Error", "請先選擇 Serial Port")
            return

        try:
            self.ser = serial.Serial(
                port=port,
                baudrate=BAUD_RATE,
                timeout=0.1,
            )

            # Arduino 開啟 Serial 後通常會 Reset
            time.sleep(2)

            self.serial_running = True
            self.serial_thread = threading.Thread(
                target=self.read_serial_loop,
                daemon=True,
            )
            self.serial_thread.start()

            self.connect_btn.config(text="Disconnect")
            self.status_label.config(text="Connected", foreground="green")
            self.log(f"Connected to {port} at {BAUD_RATE} baud", source="PC")

        except Exception as error:
            self.ser = None
            messagebox.showerror("Connection Error", str(error))
            self.log(f"Connection failed: {error}", source="Error")

    def disconnect_serial(self):
        self.stop_loop_cycle(log_message=False)
        self.serial_running = False

        with self.serial_write_lock:
            if self.ser:
                try:
                    self.ser.close()
                except Exception:
                    pass

            self.ser = None

        self.connect_btn.config(text="Connect")
        self.status_label.config(text="Disconnected", foreground="red")
        self.log("Disconnected", source="PC")

    def read_serial_loop(self):
        while self.serial_running:
            try:
                line = ""

                if self.ser and self.ser.is_open:
                    line = self.ser.readline().decode(errors="ignore").strip()

                if line:
                    self.ui_queue.put(("log", (line, "Arduino")))

                    if line == "DONE":
                        self.move_done_event.set()

            except Exception as error:
                self.ui_queue.put(("log", (f"Serial error: {error}", "Error")))
                break

    # =============================================================
    # Serial Commands
    # =============================================================
    def write_serial_command(self, command):
        command = command.strip()

        if not command:
            raise ValueError("指令不可為空白")

        with self.serial_write_lock:
            if not self.ser or not self.ser.is_open:
                raise ConnectionError("Serial 尚未連線")

            full_command = command + "\n"
            self.ser.write(full_command.encode())
            self.ser.flush()

        self.ui_queue.put(("log", (command, "PC")))

    def send_command(self, command):
        try:
            self.write_serial_command(command)

        except ConnectionError as error:
            messagebox.showwarning("Warning", str(error))
            self.log(str(error), source="Error")

        except Exception as error:
            messagebox.showerror("Send Error", str(error))
            self.log(f"Send error: {error}", source="Error")

    # =============================================================
    # Manual Motor Control
    # =============================================================
    def send_manual_move(self, motor, direction):
        try:
            steps = float(self.manual_steps_vars[motor].get())
            delay_us = float(self.manual_delay_vars[motor].get())
            ramp = float(self.manual_ramp_vars[motor].get())

            if steps <= 0:
                raise ValueError("steps 必須大於 0")

            if delay_us <= 0:
                raise ValueError("delay 必須大於 0")

            if ramp < 0:
                raise ValueError("ramp 不可小於 0")

            command = (
                f"MOVE {motor} {direction} "
                f"{steps:g} {delay_us:g} {ramp:g}"
            )
            self.send_command(command)

        except ValueError as error:
            messagebox.showerror("Input Error", str(error))
            self.log(f"Manual input error: {error}", source="Error")

    # =============================================================
    # Emergency Stop
    # =============================================================
    def send_emergency_stop(self):
        self.stop_loop_cycle(log_message=False)
        self.move_done_event.set()  # 解除可能正在等待 DONE 的 loop thread

        try:
            self.write_serial_command("STOP")
            self.log("Emergency STOP requested", source="Emergency")
        except ConnectionError as error:
            self.log(f"Emergency STOP could not be sent: {error}", source="Emergency")
            messagebox.showwarning("Emergency Stop", str(error))
        except Exception as error:
            self.log(f"Emergency STOP error: {error}", source="Error")
            messagebox.showerror("Emergency Stop Error", str(error))

    def send_move_and_wait(self, command, timeout=60):
        self.move_done_event.clear()
        self.write_serial_command(command)

        finished = self.move_done_event.wait(timeout)

        if self.loop_stop_event.is_set():
            raise InterruptedError("Loop stopped by user")

        if not finished:
            raise TimeoutError(f"Motor did not finish within {timeout} seconds")

    # =============================================================
    # Loop Cycle
    # =============================================================
    def start_loop_cycle(self):
        if self.loop_thread and self.loop_thread.is_alive():
            messagebox.showwarning("Loop Cycle", "Loop Cycle 已經在執行")
            return

        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("Warning", "Serial 尚未連線")
            return

        try:
            motor = int(self.loop_motor_var.get())
            steps = float(self.loop_steps_var.get())
            delay_us = float(self.loop_delay_us_var.get())
            ramp = float(self.loop_ramp_var.get())
            repeat_cycles = int(self.loop_cycles_var.get())
            gap_between_directions = float(self.loop_gap_dir_var.get())
            gap_between_cycles = float(self.loop_gap_cycle_var.get())
            start_direction = 0 if self.loop_start_dir_var.get() == "Dir 0" else 1

            if motor < 0 or motor >= MOTOR_COUNT:
                raise ValueError(f"motor 必須是 0～{MOTOR_COUNT - 1}")

            if steps <= 0:
                raise ValueError("steps 必須大於 0")

            if delay_us <= 0:
                raise ValueError("delay 必須大於 0")

            if ramp < 0:
                raise ValueError("ramp 不可小於 0")

            if repeat_cycles <= 0:
                raise ValueError("cycles 必須大於 0")

            if gap_between_directions < 0:
                raise ValueError("gap between directions 不可小於 0")

            if gap_between_cycles < 0:
                raise ValueError("gap between cycles 不可小於 0")

        except ValueError as error:
            messagebox.showerror("Input Error", str(error))
            self.log(f"Loop input error: {error}", source="Error")
            return

        self.loop_stop_event.clear()
        self.start_loop_btn.config(state="disabled")

        self.log(
            f"Starting loop: Motor {motor}, {repeat_cycles} cycles, "
            f"start Dir {start_direction}",
            source="Loop",
        )

        self.loop_thread = threading.Thread(
            target=self.loop_cycle_worker,
            args=(
                motor,
                steps,
                delay_us,
                ramp,
                gap_between_directions,
                gap_between_cycles,
                repeat_cycles,
                start_direction,
            ),
            daemon=True,
        )
        self.loop_thread.start()

    def loop_cycle_worker(
        self,
        motor,
        steps,
        delay_us,
        ramp,
        gap_between_directions,
        gap_between_cycles,
        repeat_cycles,
        start_direction,
    ):
        completed_cycles = 0
        stopped = False
        error_message = None

        first_direction = start_direction
        second_direction = 1 - start_direction

        command_first = (
            f"MOVE {motor} {first_direction} "
            f"{steps:g} {delay_us:g} {ramp:g}"
        )
        command_second = (
            f"MOVE {motor} {second_direction} "
            f"{steps:g} {delay_us:g} {ramp:g}"
        )

        try:
            for cycle_number in range(1, repeat_cycles + 1):
                if self.loop_stop_event.is_set():
                    stopped = True
                    break

                self.ui_queue.put((
                    "log",
                    (
                        f"Cycle {cycle_number}/{repeat_cycles} - "
                        f"Motor {motor} - Dir {first_direction}",
                        "Loop",
                    ),
                ))
                self.send_move_and_wait(command_first)

                if self.loop_stop_event.wait(gap_between_directions):
                    stopped = True
                    break

                if gap_between_directions > 0:
                    self.ui_queue.put((
                        "log",
                        (
                            f"Gap between directions: "
                            f"{gap_between_directions:g} s",
                            "Loop",
                        ),
                    ))

                self.ui_queue.put((
                    "log",
                    (
                        f"Cycle {cycle_number}/{repeat_cycles} - "
                        f"Motor {motor} - Dir {second_direction}",
                        "Loop",
                    ),
                ))
                self.send_move_and_wait(command_second)

                completed_cycles = cycle_number

                if cycle_number < repeat_cycles:
                    if gap_between_cycles > 0:
                        self.ui_queue.put((
                            "log",
                            (
                                f"Gap between cycles: "
                                f"{gap_between_cycles:g} s",
                                "Loop",
                            ),
                        ))

                    if self.loop_stop_event.wait(gap_between_cycles):
                        stopped = True
                        break

        except InterruptedError:
            stopped = True

        except Exception as error:
            error_message = str(error)

        self.ui_queue.put((
            "loop_finished",
            {
                "completed": completed_cycles,
                "total": repeat_cycles,
                "stopped": stopped,
                "error": error_message,
            },
        ))

    def stop_loop_cycle(self, log_message=True):
        if self.loop_thread and self.loop_thread.is_alive():
            self.loop_stop_event.set()
            self.move_done_event.set()

            if log_message:
                self.log("Loop stop requested", source="Loop")

    def handle_loop_finished(self, result):
        self.start_loop_btn.config(state="normal")

        completed = result["completed"]
        total = result["total"]
        stopped = result["stopped"]
        error = result["error"]

        if error:
            self.log(
                f"Loop error after {completed}/{total} cycles: {error}",
                source="Error",
            )

        elif stopped:
            self.log(
                f"Loop stopped: {completed}/{total} cycles completed",
                source="Loop",
            )

        else:
            self.log(
                f"Loop completed: {completed}/{total} cycles",
                source="Loop",
            )

    # =============================================================
    # GUI Queue
    # =============================================================
    def process_ui_queue(self):
        while not self.ui_queue.empty():
            action, data = self.ui_queue.get()

            if action == "log":
                text, source = data
                self.log(text, source=source)

            elif action == "loop_finished":
                self.handle_loop_finished(data)

        self.root.after(100, self.process_ui_queue)

    # =============================================================
    # Monitor / Close
    # =============================================================
    def log(self, text, source="Info"):
        valid_sources = {"PC", "Arduino", "Loop", "Emergency", "Error", "Info"}
        if source not in valid_sources:
            source = "Info"

        prefix = f"[{source}] "
        self.monitor.insert("end", prefix + text + "\n", source)
        self.monitor.see("end")

    def clear_monitor(self):
        self.monitor.delete("1.0", "end")

    def on_close(self):
        self.loop_stop_event.set()
        self.move_done_event.set()
        self.serial_running = False

        with self.serial_write_lock:
            if self.ser:
                try:
                    self.ser.close()
                except Exception:
                    pass

                self.ser = None

        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = StepperGUI(root)
    root.mainloop()
