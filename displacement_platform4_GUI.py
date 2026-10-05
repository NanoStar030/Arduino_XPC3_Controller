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
        self.root.geometry("820x630")

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
        # Serial Connection
        # =========================================================
        conn_frame = ttk.LabelFrame(self.root, text="Serial Connection")
        conn_frame.pack(fill="x", padx=10, pady=8)

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, padx=5, pady=5)

        self.port_var = tk.StringVar()

        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=20, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=5, pady=5)

        self.refresh_btn = ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports)
        self.refresh_btn.grid(row=0, column=2, padx=5, pady=5)

        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=3, padx=5, pady=5)

        self.status_label = ttk.Label(conn_frame, text="Disconnected", foreground="red")
        self.status_label.grid(row=0, column=4, padx=10, pady=5)

        # =========================================================
        # Manual MOVE Command
        # =========================================================
        move_frame = ttk.LabelFrame(self.root, text="Manual MOVE Command")
        move_frame.pack(fill="x", padx=10, pady=8)

        self.motor_var = tk.IntVar(value=0)

        ttk.Label(move_frame, text="Motor:").grid(row=0, column=0, padx=5, pady=5)

        for motor_index in range(MOTOR_COUNT):
            ttk.Radiobutton(move_frame, text=f"Motor {motor_index}", variable=self.motor_var, value=motor_index).grid(row=0, column=motor_index + 1, padx=5, pady=5)

        self.dir_var = tk.IntVar(value=0)

        ttk.Label(move_frame, text="Direction:").grid(row=0, column=5, padx=5, pady=5)

        ttk.Radiobutton(move_frame, text="0", variable=self.dir_var, value=0).grid(row=0, column=6, padx=5, pady=5)

        ttk.Radiobutton(move_frame, text="1", variable=self.dir_var, value=1).grid(row=0, column=7, padx=5, pady=5)

        ttk.Label(move_frame, text="Steps (steps):").grid(row=1, column=0, padx=5, pady=5)

        self.distance_var = tk.StringVar(value="1")

        ttk.Entry(move_frame, textvariable=self.distance_var, width=12).grid(row=1, column=1, padx=5, pady=5)

        ttk.Label(move_frame, text="Delay (us):").grid(row=1, column=2, padx=5, pady=5)

        self.speed_var = tk.StringVar(value="3")

        ttk.Entry(move_frame, textvariable=self.speed_var, width=12).grid(row=1, column=3, padx=5, pady=5)

        ttk.Label(move_frame, text="Ramp (steps):").grid(row=1, column=4, padx=5, pady=5)

        self.ramp_var = tk.StringVar(value="1")

        ttk.Entry(move_frame, textvariable=self.ramp_var, width=12).grid(row=1, column=5, padx=5, pady=5)

        self.move_btn = ttk.Button(move_frame, text="Send MOVE", command=self.send_move)
        self.move_btn.grid(row=2, column=0, columnspan=3, padx=5, pady=10, sticky="ew")

        self.stop_btn = ttk.Button(move_frame, text="STOP Motor", command=self.send_stop)
        self.stop_btn.grid(row=2, column=3, columnspan=3, padx=5, pady=10, sticky="ew")

        # =========================================================
        # Loop Cycle
        # =========================================================
        loop_frame = ttk.LabelFrame(self.root, text="Loop Cycle — Direction 0 → Delay → Direction 1")
        loop_frame.pack(fill="x", padx=10, pady=8)

        ttk.Label(loop_frame, text="Motor:").grid(row=0, column=0, padx=5, pady=5)

        self.loop_motor_var = tk.StringVar(value="0")

        self.loop_motor_combo = ttk.Combobox(loop_frame, textvariable=self.loop_motor_var, values=[str(i) for i in range(MOTOR_COUNT)], width=8, state="readonly")
        self.loop_motor_combo.grid(row=0, column=1, padx=5, pady=5)

        ttk.Label(loop_frame, text="Distance mm:").grid(row=0, column=2, padx=5, pady=5)

        self.loop_distance_var = tk.StringVar(value="1")

        ttk.Entry(loop_frame, textvariable=self.loop_distance_var, width=10).grid(row=0, column=3, padx=5, pady=5)

        ttk.Label(loop_frame, text="Speed mm/s:").grid(row=0, column=4, padx=5, pady=5)

        self.loop_speed_var = tk.StringVar(value="3")

        ttk.Entry(loop_frame, textvariable=self.loop_speed_var, width=10).grid(row=0, column=5, padx=5, pady=5)

        ttk.Label(loop_frame, text="Ramp mm:").grid(row=0, column=6, padx=5, pady=5)

        self.loop_ramp_var = tk.StringVar(value="1")

        ttk.Entry(loop_frame, textvariable=self.loop_ramp_var, width=10).grid(row=0, column=7, padx=5, pady=5)

        ttk.Label(loop_frame, text="Delay seconds:").grid(row=1, column=0, padx=5, pady=5)

        self.loop_delay_var = tk.StringVar(value="0.5")

        ttk.Entry(loop_frame, textvariable=self.loop_delay_var, width=10).grid(row=1, column=1, padx=5, pady=5)

        ttk.Label(loop_frame, text="Repeat cycles:").grid(row=1, column=2, padx=5, pady=5)

        self.loop_repeat_var = tk.StringVar(value="10")

        ttk.Entry(loop_frame, textvariable=self.loop_repeat_var, width=10).grid(row=1, column=3, padx=5, pady=5)

        self.start_loop_btn = ttk.Button(loop_frame, text="Start Loop", command=self.start_loop_cycle)
        self.start_loop_btn.grid(row=1, column=4, columnspan=2, padx=5, pady=5, sticky="ew")

        self.stop_loop_btn = ttk.Button(loop_frame, text="Stop Loop", command=self.stop_loop_cycle, state="disabled")
        self.stop_loop_btn.grid(row=1, column=6, columnspan=2, padx=5, pady=5, sticky="ew")

        self.loop_status_var = tk.StringVar(value="Idle")

        ttk.Label(loop_frame, text="Loop Status:").grid(row=2, column=0, padx=5, pady=5)

        ttk.Label(loop_frame, textvariable=self.loop_status_var).grid(row=2, column=1, columnspan=7, padx=5, pady=5, sticky="w")

        # =========================================================
        # Serial Monitor
        # =========================================================
        monitor_frame = ttk.LabelFrame(self.root, text="Serial Monitor")
        monitor_frame.pack(fill="both", expand=True, padx=10, pady=8)

        self.monitor = tk.Text(monitor_frame, height=15, wrap="word")
        self.monitor.pack(side="left", fill="both", expand=True, padx=5, pady=5)

        scrollbar = ttk.Scrollbar(monitor_frame, command=self.monitor.yview)
        scrollbar.pack(side="right", fill="y")

        self.monitor.config(yscrollcommand=scrollbar.set)

        # =========================================================
        # Bottom Buttons
        # =========================================================
        bottom_frame = ttk.Frame(self.root)
        bottom_frame.pack(fill="x", padx=10, pady=5)

        ttk.Button(bottom_frame, text="Clear Monitor", command=self.clear_monitor).pack(side="left")

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
            self.ser = serial.Serial(port=port, baudrate=BAUD_RATE, timeout=0.1)

            # Arduino 開啟 Serial 後通常會 Reset
            time.sleep(2)

            self.serial_running = True

            self.serial_thread = threading.Thread(target=self.read_serial_loop, daemon=True)
            self.serial_thread.start()

            self.connect_btn.config(text="Disconnect")

            self.status_label.config(text="Connected", foreground="green")

            self.log(f"[PC] Connected to {port} " f"at {BAUD_RATE} baud")

        except Exception as error:
            self.ser = None

            messagebox.showerror("Connection Error", str(error))

    def disconnect_serial(self):
        self.stop_loop_cycle()

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

        self.log("[PC] Disconnected")

    def read_serial_loop(self):
        while self.serial_running:
            try:
                if self.ser and self.ser.is_open:
                    line = (self.ser.readline().decode(errors="ignore").strip())

                if line:
                    self.ui_queue.put(("log", f"[Arduino] {line}"))

                    if line == "DONE":
                        self.move_done_event.set()

            except Exception as error:
                self.ui_queue.put(("log", f"[Serial Error] {error}"))
                break

    # =============================================================
    # Serial Command
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

        self.ui_queue.put(("log", f"[PC] {command}"))

    def send_command(self, command):
        try:
            self.write_serial_command(command)

        except ConnectionError as error:
            messagebox.showwarning("Warning", str(error))

        except Exception as error:
            messagebox.showerror("Send Error", str(error))

    # =============================================================
    # Manual MOVE
    # =============================================================
    def send_move(self):
        try:
            motor = self.motor_var.get()
            direction = self.dir_var.get()
            distance = float(self.distance_var.get())
            speed = float(self.speed_var.get())
            ramp = float(self.ramp_var.get())

            if motor < 0 or motor >= MOTOR_COUNT:
                raise ValueError(f"motor 必須是 0～{MOTOR_COUNT - 1}")

            if direction not in (0, 1):
                raise ValueError("direction 必須是 0 或 1")

            if distance <= 0:
                raise ValueError("distance 必須大於 0")

            if speed <= 0:
                raise ValueError("speed 必須大於 0")

            if ramp < 0:
                raise ValueError("ramp 不可小於 0")

            command = (f"MOVE {motor} {direction} " f"{distance:g} {speed:g} {ramp:g}")

            self.send_command(command)

        except ValueError as error:
            messagebox.showerror("Input Error", str(error))

    def send_stop(self):
        self.stop_loop_cycle()
        self.send_command("STOP")


    def send_move_and_wait(self, command, timeout=60):
        self.move_done_event.clear()

        self.write_serial_command(command)

        finished = self.move_done_event.wait(timeout)

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
            distance = float(self.loop_distance_var.get())
            speed = float(self.loop_speed_var.get())
            ramp = float(self.loop_ramp_var.get())
            delay_seconds = float(self.loop_delay_var.get())
            repeat_cycles = int(self.loop_repeat_var.get())

            if motor < 0 or motor >= MOTOR_COUNT:
                raise ValueError(f"motor 必須是 0～{MOTOR_COUNT - 1}")

            if distance <= 0:
                raise ValueError("distance 必須大於 0")

            if speed <= 0:
                raise ValueError("speed 必須大於 0")

            if ramp < 0:
                raise ValueError("ramp 不可小於 0")

            if delay_seconds < 0:
                raise ValueError("delay 不可小於 0")

            if repeat_cycles <= 0:
                raise ValueError("repeat cycles 必須大於 0")

        except ValueError as error:
            messagebox.showerror("Input Error", str(error))
            return

        self.loop_stop_event.clear()

        self.start_loop_btn.config(state="disabled")
        self.stop_loop_btn.config(state="normal")

        self.loop_status_var.set(f"Starting 0/{repeat_cycles}")

        self.loop_thread = threading.Thread(target=self.loop_cycle_worker, args=(motor, distance, speed, ramp, delay_seconds, repeat_cycles), daemon=True)
        self.loop_thread.start()

    def loop_cycle_worker(self, motor, distance, speed, ramp, delay_seconds, repeat_cycles):
        completed_cycles = 0
        stopped = False
        error_message = None
        delay_seconds2 = delay_seconds*2
        command_direction_0 = (f"MOVE {motor} 0 " f"{distance:g} {speed:g} {ramp:g}")

        command_direction_1 = (f"MOVE {motor} 1 " f"{distance:g} {speed:g} {ramp:g}")

        try:
            for cycle_number in range(1, repeat_cycles + 1):
                if self.loop_stop_event.is_set():
                    stopped = True
                    break

                # Direction 0
                self.ui_queue.put(("loop_status", f"Cycle {cycle_number}/{repeat_cycles}: " f"Motor {motor}, Direction 0"))

                self.send_move_and_wait(command_direction_0)

                # Motor 0 finished → wait 0.5 s
                if self.loop_stop_event.wait(delay_seconds/2):
                    stopped = True
                    break

                # Direction 1
                self.ui_queue.put(("loop_status", f"Cycle {cycle_number}/{repeat_cycles}: " f"Motor {motor}, Direction 1"))
                self.send_move_and_wait(command_direction_1)

                completed_cycles = cycle_number

                # Motor 1 finished → wait 0.5 s
                if cycle_number < repeat_cycles:
                    if self.loop_stop_event.wait(delay_seconds):
                        stopped = True
                        break

        except Exception as error:
            error_message = str(error)

        self.ui_queue.put(("loop_finished", { "completed": completed_cycles, "total": repeat_cycles, "stopped": stopped, "error": error_message }))

    def stop_loop_cycle(self):
        if self.loop_thread and self.loop_thread.is_alive():
            self.loop_stop_event.set()
            self.loop_status_var.set("Stopping...")

    def handle_loop_finished(self, result):
        self.start_loop_btn.config(state="normal")
        self.stop_loop_btn.config(state="disabled")

        completed = result["completed"]
        total = result["total"]
        stopped = result["stopped"]
        error = result["error"]

        if error:
            self.loop_status_var.set(f"Error after {completed}/{total} cycles")

            self.log(f"[Loop Error] {error}")

        elif stopped:
            self.loop_status_var.set(f"Stopped: {completed}/{total} cycles completed")

            self.log(f"[Loop] Stopped after " f"{completed}/{total} cycles")

        else:
            self.loop_status_var.set(f"Completed: {completed}/{total} cycles")

            self.log(f"[Loop] Completed " f"{completed}/{total} cycles")

    # =============================================================
    # GUI Queue
    # =============================================================
    def process_ui_queue(self):
        while not self.ui_queue.empty():
            action, data = self.ui_queue.get()

            if action == "log":
                self.log(data)

            elif action == "loop_status":
                self.loop_status_var.set(data)

            elif action == "loop_finished":
                self.handle_loop_finished(data)

        self.root.after(100, self.process_ui_queue)

    # =============================================================
    # Monitor / Close
    # =============================================================
    def log(self, text):
        self.monitor.insert("end", text + "\n")
        self.monitor.see("end")

    def clear_monitor(self):
        self.monitor.delete("1.0", "end")

    def on_close(self):
        self.loop_stop_event.set()
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
