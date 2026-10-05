import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time


BAUD_RATE = 115200
MOTOR_COUNT = 3


class StepperGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Arduino 2-Motor Stepper Controller")
        self.root.geometry("760x540")

        self.ser = None
        self.serial_thread = None
        self.serial_running = False
        self.rx_queue = queue.Queue()

        self.create_widgets()
        self.refresh_ports()
        self.root.after(100, self.process_serial_queue)

    def create_widgets(self):
        # ===== Connection Frame =====
        conn_frame = ttk.LabelFrame(self.root, text="Serial Connection")
        conn_frame.pack(fill="x", padx=10, pady=8)

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, padx=5, pady=5)

        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            conn_frame,
            textvariable=self.port_var,
            width=20,
            state="readonly"
        )
        self.port_combo.grid(row=0, column=1, padx=5, pady=5)

        self.refresh_btn = ttk.Button(
            conn_frame,
            text="Refresh",
            command=self.refresh_ports
        )
        self.refresh_btn.grid(row=0, column=2, padx=5, pady=5)

        self.connect_btn = ttk.Button(
            conn_frame,
            text="Connect",
            command=self.toggle_connection
        )
        self.connect_btn.grid(row=0, column=3, padx=5, pady=5)

        self.status_label = ttk.Label(
            conn_frame,
            text="Disconnected",
            foreground="red"
        )
        self.status_label.grid(row=0, column=4, padx=10, pady=5)

        # ===== Move Frame =====
        move_frame = ttk.LabelFrame(self.root, text="MOVE Command")
        move_frame.pack(fill="x", padx=10, pady=8)

        # Motor selection
        self.motor_var = tk.IntVar(value=0)

        ttk.Label(move_frame, text="Motor:").grid(row=0, column=0, padx=5, pady=5)

        ttk.Radiobutton(
            move_frame,
            text="Motor 0",
            variable=self.motor_var,
            value=0
        ).grid(row=0, column=1, padx=5, pady=5)

        ttk.Radiobutton(
            move_frame,
            text="Motor 1",
            variable=self.motor_var,
            value=1
        ).grid(row=0, column=2, padx=5, pady=5)

        ttk.Radiobutton(
            move_frame,
            text="Motor 2",
            variable=self.motor_var,
            value=2
        ).grid(row=0, column=3, padx=5, pady=5)

        # Direction selection
        self.dir_var = tk.IntVar(value=1)

        ttk.Label(move_frame, text="Direction:").grid(row=0, column=4, padx=5, pady=5)

        ttk.Radiobutton(
            move_frame,
            text="0",
            variable=self.dir_var,
            value=0
        ).grid(row=0, column=5, padx=5, pady=5)

        ttk.Radiobutton(
            move_frame,
            text="1",
            variable=self.dir_var,
            value=1
        ).grid(row=0, column=6, padx=5, pady=5)

        ttk.Label(move_frame, text="Distance mm:").grid(row=1, column=0, padx=5, pady=5)
        self.distance_var = tk.StringVar(value="5")
        ttk.Entry(
            move_frame,
            textvariable=self.distance_var,
            width=12
        ).grid(row=1, column=1, padx=5, pady=5)

        ttk.Label(move_frame, text="Speed mm/s:").grid(row=1, column=2, padx=5, pady=5)
        self.speed_var = tk.StringVar(value="3")
        ttk.Entry(
            move_frame,
            textvariable=self.speed_var,
            width=12
        ).grid(row=1, column=3, padx=5, pady=5)

        ttk.Label(move_frame, text="Ramp mm:").grid(row=1, column=4, padx=5, pady=5)
        self.ramp_var = tk.StringVar(value="1")
        ttk.Entry(
            move_frame,
            textvariable=self.ramp_var,
            width=12
        ).grid(row=1, column=5, padx=5, pady=5)

        self.move_btn = ttk.Button(
            move_frame,
            text="Send MOVE",
            command=self.send_move
        )
        self.move_btn.grid(row=2, column=0, columnspan=3, padx=5, pady=10, sticky="ew")

        self.stop_btn = ttk.Button(
            move_frame,
            text="STOP",
            command=self.send_stop
        )
        self.stop_btn.grid(row=2, column=3, columnspan=3, padx=5, pady=10, sticky="ew")

        # ===== Quick Command Frame =====
        cmd_frame = ttk.LabelFrame(self.root, text="Quick Commands")
        cmd_frame.pack(fill="x", padx=10, pady=8)

        ttk.Button(
            cmd_frame,
            text="PING",
            command=lambda: self.send_command("PING")
        ).grid(row=0, column=0, padx=5, pady=5)

        ttk.Button(
            cmd_frame,
            text="STATUS",
            command=lambda: self.send_command("STATUS")
        ).grid(row=0, column=1, padx=5, pady=5)

        ttk.Button(
            cmd_frame,
            text="GET_CONFIG",
            command=lambda: self.send_command("GET_CONFIG")
        ).grid(row=0, column=2, padx=5, pady=5)

        ttk.Label(cmd_frame, text="Custom command:").grid(row=1, column=0, padx=5, pady=5)

        self.custom_cmd_var = tk.StringVar()
        ttk.Entry(
            cmd_frame,
            textvariable=self.custom_cmd_var,
            width=55
        ).grid(row=1, column=1, columnspan=3, padx=5, pady=5, sticky="ew")

        ttk.Button(
            cmd_frame,
            text="Send",
            command=self.send_custom_command
        ).grid(row=1, column=4, padx=5, pady=5)

        # ===== Serial Monitor =====
        monitor_frame = ttk.LabelFrame(self.root, text="Serial Monitor")
        monitor_frame.pack(fill="both", expand=True, padx=10, pady=8)

        self.monitor = tk.Text(
            monitor_frame,
            height=15,
            wrap="word"
        )
        self.monitor.pack(side="left", fill="both", expand=True, padx=5, pady=5)

        scrollbar = ttk.Scrollbar(
            monitor_frame,
            command=self.monitor.yview
        )
        scrollbar.pack(side="right", fill="y")

        self.monitor.config(yscrollcommand=scrollbar.set)

        # ===== Bottom Buttons =====
        bottom_frame = ttk.Frame(self.root)
        bottom_frame.pack(fill="x", padx=10, pady=5)

        ttk.Button(
            bottom_frame,
            text="Clear Monitor",
            command=self.clear_monitor
        ).pack(side="left")

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

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
                timeout=0.1
            )

            time.sleep(2)  # Arduino reset 後稍等一下

            self.serial_running = True
            self.serial_thread = threading.Thread(
                target=self.read_serial_loop,
                daemon=True
            )
            self.serial_thread.start()

            self.connect_btn.config(text="Disconnect")
            self.status_label.config(text="Connected", foreground="green")

            self.log(f"[PC] Connected to {port} at {BAUD_RATE} baud")

        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def disconnect_serial(self):
        self.serial_running = False

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
                    line = self.ser.readline().decode(errors="ignore").strip()

                    if line:
                        self.rx_queue.put(line)

            except Exception as e:
                self.rx_queue.put(f"[Serial Error] {e}")
                break

    def process_serial_queue(self):
        while not self.rx_queue.empty():
            line = self.rx_queue.get()
            self.log(f"[Arduino] {line}")

        self.root.after(100, self.process_serial_queue)

    def send_command(self, cmd):
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("Warning", "Serial 尚未連線")
            return

        try:
            full_cmd = cmd.strip() + "\n"
            self.ser.write(full_cmd.encode())
            self.ser.flush()
            self.log(f"[PC] {cmd.strip()}")

        except Exception as e:
            messagebox.showerror("Send Error", str(e))

    def send_move(self):
        try:
            motor = self.motor_var.get()
            direction = self.dir_var.get()
            distance = float(self.distance_var.get())
            speed = float(self.speed_var.get())
            ramp = float(self.ramp_var.get())

            if motor < 0 or motor >= MOTOR_COUNT:
                messagebox.showerror("Input Error", "motor 必須是 0 或 1")
                return

            if direction not in (0, 1):
                messagebox.showerror("Input Error", "direction 必須是 0 或 1")
                return

            if distance <= 0:
                messagebox.showerror("Input Error", "distance 必須大於 0")
                return

            if speed <= 0:
                messagebox.showerror("Input Error", "speed 必須大於 0")
                return

            if ramp < 0:
                messagebox.showerror("Input Error", "ramp 不可小於 0")
                return

            # Arduino 指令格式：
            # MOVE motor dir distance speed ramp
            cmd = f"MOVE {motor} {direction} {distance} {speed} {ramp}"
            self.send_command(cmd)

        except ValueError:
            messagebox.showerror("Input Error", "請確認 distance、speed、ramp 都是數字")

    def send_stop(self):
        self.send_command("STOP")

    def send_custom_command(self):
        cmd = self.custom_cmd_var.get().strip()

        if not cmd:
            return

        self.send_command(cmd)

    def log(self, text):
        self.monitor.insert("end", text + "\n")
        self.monitor.see("end")

    def clear_monitor(self):
        self.monitor.delete("1.0", "end")

    def on_close(self):
        self.disconnect_serial()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = StepperGUI(root)
    root.mainloop()