# 🧠 Reversi (Othello) Game in C

This project is a terminal-based implementation of the classic Reversi (Othello) game written in C.

## 🎮 Features

- 8x8 Reversi game board
- User account system
- PIN security with hashing (djb2)
- Save / Load game system
- Checksum validation (anti-cheating)
- Greedy AI opponent
- Menu-based interface

---

## 🔐 Account System

- Users must create an account and log in
- PIN is NOT stored directly
- PIN is hashed using djb2 algorithm
- After 3 incorrect attempts → program exits

---

## 💾 Save & Load

- All games stored in:
  - `saved_games.dat`
- User accounts stored in:
  - `accounts.dat`

### Supported:
- Save
- Save As (new record)
- Load game
- List user saves

---

## 🛡️ Security (Checksum)

- Each game state includes a checksum
- Prevents manual file manipulation
- If mismatch → "corrupted/tampered" error

---

## 🤖 AI (Greedy)

Computer:
- Checks all possible moves
- Selects the move that flips the **maximum pieces**

---

## ⚙️ How to Run

### Compile:
```bash
gcc reversi_assignment.c -o oyun
📂 Files
	•	reversi_assignment.c → main source code
	•	accounts.dat → user accounts
	•	saved_games.dat → saved games
