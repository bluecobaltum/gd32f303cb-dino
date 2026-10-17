"""Desktop preview of the GD32 128x32 Dino game (no board required)."""

import tkinter as tk

SCALE = 6
WIDTH, HEIGHT = 128, 32


class DinoPreview:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("GD32 Dino - OLED 128x32 preview")
        self.canvas = tk.Canvas(self.root, width=WIDTH * SCALE,
                                height=HEIGHT * SCALE, bg="#081018",
                                highlightthickness=0)
        self.canvas.pack(padx=12, pady=(12, 4))
        tk.Label(self.root, text="Space / ↑: Jump     ↓: Duck     R: Restart     P: Pause",
                 font=("Segoe UI", 11)).pack(pady=(0, 10))
        self.keys = set()
        self.last_jump = False
        self.last_restart = False
        self.last_pause = False
        self.pause_cooldown = 0
        self.reset()
        self.root.bind("<KeyPress>", self.key_down)
        self.root.bind("<KeyRelease>", self.key_up)
        self.root.focus_force()
        self.tick()

    def reset(self):
        self.obstacle_x = 127
        self.coin_x = 95
        self.coin_active = True
        self.coin_multiplier = 1
        self.obstacles_cleared = 0
        self.jump_y = 0
        self.jump_velocity = 0
        self.score = 0
        self.game_over = False
        self.game_paused = False
        self.duck_pose = False

    def key_down(self, event):
        self.keys.add(event.keysym.lower())

    def key_up(self, event):
        self.keys.discard(event.keysym.lower())

    def pixel(self, x, y):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            self.canvas.create_rectangle(x * SCALE, y * SCALE,
                                         (x + 1) * SCALE, (y + 1) * SCALE,
                                         fill="#d8f7ff", outline="")

    def rect(self, x, y, w, h):
        if w and h:
            self.canvas.create_rectangle(x * SCALE, y * SCALE,
                                         (x + w) * SCALE, (y + h) * SCALE,
                                         fill="#d8f7ff", outline="")

    def digit(self, value, x):
        glyphs = ((31, 17, 31), (0, 31, 0), (29, 21, 23),
                  (21, 21, 31), (7, 4, 31), (23, 21, 29),
                  (31, 21, 29), (1, 1, 31), (31, 21, 31), (23, 21, 31))
        for col, bits in enumerate(glyphs[value]):
            for row in range(5):
                if bits & (1 << row):
                    self.pixel(x + col, 2 + row)

    def update(self, jump, duck, restart, pause):
        jump_edge = jump and not self.last_jump
        restart_edge = restart and not self.last_restart
        pause_edge = pause and not self.last_pause
        self.last_jump, self.last_restart = jump, restart
        self.last_pause = pause
        if restart_edge:
            self.reset()
            return
        if self.pause_cooldown:
            self.pause_cooldown -= 1
        elif pause_edge and not self.game_over:
            self.game_paused = not self.game_paused
            self.pause_cooldown = 4
        if self.game_paused:
            return
        if self.game_over:
            return
        self.duck_pose = duck
        if jump_edge and self.jump_y == 0 and not duck:
            self.jump_velocity = 11
        if duck and self.jump_y > 0 and self.jump_velocity > -10:
            self.jump_velocity = -10
        if self.jump_y > 0 or self.jump_velocity > 0:
            next_y = self.jump_y + self.jump_velocity
            self.jump_velocity -= 2
            if next_y <= 0:
                self.jump_y = self.jump_velocity = 0
            else:
                self.jump_y = next_y
        speed = min(7, 3 + self.obstacles_cleared // 4)
        self.obstacle_x -= speed
        self.coin_x -= speed
        if self.obstacle_x < -8:
            self.obstacle_x = 127
            self.coin_x = 95
            self.coin_active = True
            self.obstacles_cleared += 1
        if self.obstacle_x < 25 and self.obstacle_x + 8 > 12:
            if self.obstacles_cleared % 2 == 0:
                self.game_over = self.jump_y < 18
            elif not (duck and self.jump_y == 0) and self.jump_y < 26:
                self.game_over = True
        if self.coin_active and not self.game_over:
            if self.coin_x < 25 and self.coin_x + 5 > 12 and self.jump_y >= 6:
                self.score += self.coin_multiplier
                self.coin_active = False
                self.coin_multiplier = min(8, self.coin_multiplier * 2)
            elif self.coin_x < 8:
                self.coin_active = False
                self.coin_multiplier = 1

    def draw(self):
        self.canvas.delete("all")
        self.rect(0, 29, 128, 1)
        top = 18 - self.jump_y // 2
        if self.duck_pose and self.jump_y == 0:
            self.rect(12, 24, 13, 5)
            self.pixel(22, 25)
        else:
            self.rect(13, top + 3, 7, 8)
            self.rect(17, top, 8, 5)
            self.rect(10, top + 8, 4, 2)
            self.rect(14, top + 11, 2, 2)
            self.rect(19, top + 11, 2, 2)
            self.pixel(22, top + 1)
        if self.obstacles_cleared % 2 == 0:
            self.rect(self.obstacle_x, 20, 5, 9)
            self.rect(self.obstacle_x - 2, 22, 2, 4)
        else:
            self.rect(self.obstacle_x, 18, 8, 3)
            self.pixel(self.obstacle_x + 2, 17)
            self.pixel(self.obstacle_x + 5, 16)
        if self.coin_active:
            for dx, dy in ((2, 12), (1, 13), (3, 13), (0, 14),
                           (4, 14), (1, 15), (3, 15), (2, 16)):
                self.pixel(self.coin_x + dx, dy)
        for x, y in ((1, 2), (3, 4), (1, 4), (3, 2)):
            self.pixel(x, y)
        self.digit(self.coin_multiplier, 5)
        number = self.score
        for n in range(4):
            self.digit(number % 10, 121 - n * 5)
            number //= 10
        if self.game_over:
            for n in range(8):
                self.pixel(60 + n, 10 + n)
                self.pixel(67 - n, 10 + n)
        if self.game_paused:
            self.rect(59, 10, 3, 12)
            self.rect(66, 10, 3, 12)

    def tick(self):
        jump = bool(self.keys & {"space", "up"})
        duck = "down" in self.keys
        restart = "r" in self.keys
        pause = "p" in self.keys
        self.update(jump, duck, restart, pause)
        self.draw()
        self.root.after(60, self.tick)


if __name__ == "__main__":
    preview = DinoPreview()
    preview.root.mainloop()
