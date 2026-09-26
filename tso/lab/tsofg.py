"""Drive one TSO foreground session through s3270 and log every screen.

Usage (from the repo root, needs s3270 from the x3270 suite):

    TSOFG_USER=MVSCE01 TSOFG_PASS=... python3 tso/lab/tsofg.py <logfile> \
        "CALL 'IBMUSER.REXX370.V1R0M0D.TESTLIB(TSTIOTSO)' '1'"

Logs on (credentials from TSOFG_USER/TSOFG_PASS, else the .env), runs each
command at READY (answering '***' with Enter), logs off, and writes every
screen to <logfile> with the password masked. A command '@ENTER' answers a
prompt (REENTER) with an empty line, '@PA1' sends attention. If a run
aborts, the session is cancelled through the mvsMF console (C U=user), so
the user ID is not left IN USE.

Safety: it types only into a FRESH logon screen ('TSO Logon ===>'). A 3270
device keeps a TSO session after the TCP connection drops; typing into such a
screen drives somebody else's session. Any unrecognised screen stops the run.
Use a user ID of its own -- IBMUSER is often logged on (IKJ56425I IN USE).

TSOFG_HOST overrides the TN3270 address (default: MVSCE-LAB). Why this exists: the batch TMP cannot show
whether output reaches a terminal, and a test that only checks RCs cannot
show whether output arrives at all (knowledge base MVS-TSO-0001, PM-2026-004).
"""
import os
import subprocess
import sys

sys.path.insert(0, "mbt/scripts")
from mbt.config import MbtConfig  # noqa: E402

HOST = os.environ.get("TSOFG_HOST", "mvsdev.lan:3272")
MAX_SCREENS = 40

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(2)

cfg = MbtConfig("project.toml")
USER = os.environ.get("TSOFG_USER", cfg.mvs_user)
PW = os.environ.get("TSOFG_PASS", cfg.mvs_pass)
log_path, commands = sys.argv[1], sys.argv[2:]
log = open(log_path, "w")


def mask(text):
    return text.replace(PW, "********") if PW else text


class S3270:
    def __init__(self):
        self.p = subprocess.Popen(["s3270"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)

    def cmd(self, c):
        self.p.stdin.write(c + "\n")
        self.p.stdin.flush()
        data = []
        while True:
            line = self.p.stdout.readline()
            if line == "":
                raise RuntimeError(f"s3270 died on {mask(c)}")
            line = line.rstrip("\n")
            if line in ("ok", "error"):
                return line == "ok", data
            if line.startswith("data: "):
                data.append(line[6:])
            elif line.startswith("data:"):
                data.append("")

    def screen(self):
        ok, data = self.cmd("Ascii()")
        return data

    def close(self):
        try:
            self.cmd("Quit()")
        except Exception:
            pass
        self.p.wait(timeout=10)


n_screens = 0


def snap(s, label):
    global n_screens
    n_screens += 1
    scr = s.screen()
    log.write(f"===== screen {n_screens}: {label} =====\n")
    for l in scr:
        log.write(mask(l).rstrip() + "\n")
    log.flush()
    if n_screens > MAX_SCREENS:
        raise RuntimeError("too many screens")
    return [l.rstrip() for l in scr]


def settle(s, timeout=60):
    """Wait until the screen has content and stops changing."""
    import time
    last, stable, t0 = None, 0, time.time()
    while time.time() - t0 < timeout:
        s.cmd("Wait(2,Output)")
        scr = s.screen()
        if any(l.strip() for l in scr) and scr == last:
            stable += 1
            if stable >= 2:
                return
        else:
            stable = 0
        last = scr
    # fall through: the caller snaps whatever is there


def press_enter(s):
    s.cmd("Enter()")
    settle(s)


def last_line(scr):
    lines = [l.strip() for l in scr if l.strip()]
    return lines[-1] if lines else ""


def at_ready(scr):
    return last_line(scr) == "READY"


def more(scr):
    return last_line(scr) == "***"


def reenter(scr):
    # IKJPARS asks for a corrected operand; the next command answers it
    return last_line(scr).startswith("REENTER")


def run_until_ready(s, label):
    """Answer '***' until READY; stop on anything unknown."""
    for _ in range(MAX_SCREENS):
        scr = snap(s, label)
        text = "\n".join(scr)
        if "IN USE" in text or "NOT AUTHORIZED" in text or "PASSWORD" in text.upper() and "INVALID" in text.upper():
            raise RuntimeError("logon refused, see log")
        if more(scr):
            press_enter(s)
            continue
        if at_ready(scr) or reenter(scr):
            return scr
        # a screen with an input prompt we do not know: stop
        raise RuntimeError(f"unrecognised screen during {label}, see log")
    raise RuntimeError("no READY")


s = S3270()
rc = 1
try:
    s.cmd(f"Connect({HOST})")
    settle(s, 20)
    scr = snap(s, "greeting")
    text = "\n".join(scr)
    # Only ever type into a FRESH device. A device that still carries a
    # disconnected session shows that session's screen instead; typing
    # there would drive someone else's TSO.
    if "Logon ===>" not in text:
        if "CLEAR the screen or hit ENTER" not in text:
            raise RuntimeError("not a fresh logon screen -- refusing to type")
        press_enter(s)
        scr = snap(s, "logon prompt")
        if "Logon ===>" not in "\n".join(scr):
            raise RuntimeError("no 'Logon ===>' prompt -- refusing to type")
    s.cmd(f'String("LOGON {USER}/{PW}")')
    press_enter(s)
    run_until_ready(s, "logon")
    for c in commands:
        # @ENTER answers a prompt with an empty line, @PA1 is attention
        if c == "@PA1":
            s.cmd("PA(1)")
            settle(s)
        else:
            if c != "@ENTER":
                s.cmd(f'String("{c}")')
            press_enter(s)
        run_until_ready(s, c)
    s.cmd('String("LOGOFF")')
    s.cmd("Enter()")
    s.cmd("Wait(15,Disconnect)")
    rc = 0
except Exception as e:
    log.write(f"##### ABORT: {mask(str(e))}\n")
    print("ABORT:", mask(str(e)))
    # Never leave a half-driven session behind: a stuck prompt keeps the
    # user ID IN USE (IKJ56425I). Cancel it through the mvsMF console --
    # unless the abort was the logon being refused, then it is not ours.
    if "logon refused" not in str(e):
        try:
            from mbt.mvsmf import MvsMFClient
            con = MvsMFClient(host=cfg.mvs_host, port=cfg.mvs_port,
                              user=cfg.mvs_user, password=cfg.mvs_pass)
            r = con._json_request("PUT", "/restconsoles/consoles/defcn",
                                  {"cmd": f"C U={USER}"})
            log.write(f"##### CANCEL: {r.get('cmd-response', '')}\n")
            print("cancelled", USER)
        except Exception as ce:
            print("cancel failed:", ce)
finally:
    s.close()
    log.close()
print("screens:", n_screens, "rc:", rc)
sys.exit(rc)
