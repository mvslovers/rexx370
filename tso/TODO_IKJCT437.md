# IKJCT437 — Sprachentscheidung beim impliziten Aufruf

## Stand 2026-09-25 (Abend): Phase 3 — explizites EXEC, Regeln wie z/OS

Die Regeln sind auf z/OS gemessen (Mike, Z07850, 2026-09-25). SC28-1883-0 nennt nur
*"the exec keyword operand"* und keine Syntax.

| Aufruf | Entscheidung |
|---|---|
| implizit, Member in SYSPROC | REXX nur mit `/* … REXX … */` in Zeile 1, sonst CLIST |
| implizit, jeder andere DD (SYSEXEC, LOADDD) | REXX, ohne Prüfung (auch eine CLIST läuft dort als REXX) |
| `EXEC 'ds(mem)'` | REXX nur mit diesem Kommentar, **egal aus welcher Bibliothek** |
| `EXEC 'ds(mem)' EXEC` (auch `E`, `EX`, `EXE`) | REXX, erzwungen |
| `EXEC name EXEC` | Suffix `.EXEC` statt `.CLIST` |

Umsetzung:

- **PARS**: `OPER3 IKJKEYWD / IKJNAME 'EXEC'`. Das PDE liegt bei PDL+44
  (`PAR1PDL`), die älteren Felder verschieben sich nicht. Die Abkürzungen
  nimmt IKJPARS von selbst an.
- **Suffix**: eigener Zweig nach `OI DA08UID`. Ohne Schlüsselwort läuft der
  IBM-Code unverändert.
- **Expliziter Hook** nach dem Übernehmen des Member-Namens (`EX R4,@SM01405`):
  - `IKJCT43C` prüft Zeile 1, `IKJCT43F` erzwingt REXX.
  - R1 zeigt auf `PROCNAME` und `INDDNAME`, die in `@DATD` direkt hintereinander
    liegen.
  - Nach einem REXX-Lauf verlässt IKJCT430 die Verarbeitung über `@RC00450`.
    Der IBM-Code gibt dort PDL und DD frei (`FREEPDL`, `UNALLOC`).
- **IKJCT437**: drei Einstiege, R7 trägt den Modus. Implizit gilt jetzt "nicht
  SYSPROC → REXX", denn mit #241 kann der LOADDD anders heißen als SYSEXEC.
- **IKJEFTRX**: Die Meldung IKJ56942I beim Logon ist entfernt.
- **Nicht unterstützt**: ein sequenzieller Dataset beim expliziten Aufruf. Der
  BPAM-Leser braucht ein Member, also bleibt es CLIST (steht im Cover Letter).

Tests: `tso/lab/exec_test.py` mit 23 Fällen (Tabelle im Skript).

| Kombination | Job | Ergebnis |
|---|---|---|
| neuer TMP + neues EXEC | JOB01291 | 23/23 |
| IBM-TMP (aus der Sicherung vor ZMG0002) + neues EXEC | JOB01296 | 23/23 |

Unter dem IBM-TMP fällt alles auf CLIST zurück, und das Schlüsselwort wird
trotzdem angenommen. Im Batch-TMP gibt es kein Präfix, der Test setzt
`PROFILE PREFIX(IBMUSER)`. Die Meldung `IKJ56228I` von MVS 3.8 zeigt den Namen
vor dem Präfix, DAIR hängt es trotzdem an.

Nicht nachgebildet: `IKJ56479I … OR REXX IDENTIFIER IS MISSING`. Die Meldung
kommt vom TMP beim CLIST-Befehl. Unter 3.8 erscheint `IKJ56500I`.


## Stand 2026-09-25 (Nachmittag): EXEC-Hook fertig, beide Kombinationen grün

**Weg (b): kein Neubau von EXEC.** Ausgeliefert werden nur Objekt-Decks
(IKJCT430 mit PARS, dazu IKJCT437). Gebunden wird auf MVS gegen das
**installierte** `SYS1.CMDLIB(EXEC)`, sodass ZP60014 (IKJCT431), UZ25767
(IKJCT432) und IKJCT435 unverändert bleiben. Dass SMP genau so bindet
(`INCLUDE <ziel>(<lmod>)` nach den neuen Elementen), ist gemessen:
KB `MVS-SMP-0004`.

- **Referenz-Nachweis:** `tso/lmod_link.py reference exec`. Die mvs38src-Quelle,
  gebunden wie SMP bindet, ist byte-gleich mit dem installierten EXEC, UY16532
  eingeschlossen (JOB01216). Die Gegenprobe mit dem gepatchten Deck muss
  abweichen und tut es (JOB01218).
- **`RXHANDLD` ist fertig.** Bei R15=4 von IKJCT437 geht es mit R5=0 zum
  gemeinsamen Ausgang `@RC00450`, wie bei einer leeren Prozedur. Beim Hook ist
  noch nichts belegt, geöffnet oder gestapelt. Kein Aufräum-Flag ist gesetzt
  (geprüft an allen `OI`-Stellen), also läuft nichts mehr in den CLIST-Pfad.
- **Die Probe-WTOs C1..C9 sind entfernt.** IKJCT437 schrumpft von 0x38C auf
  0x2A4. C9 stand zwischen `CLC` und `BE`, der Sprung hing also davon ab, dass
  der Condition Code den SVC überlebt.

**Gefunden und behoben: feste Displacements in der ZAP-Rekonstruktion.**
Die mvs38src-Quelle von IKJCT430 bildet UY16532 mit zwei Zahlen statt Symbolen
nach: `C R2,3932(,R12)` und `BE 1928(,R12)`. Unverändert assembliert ist das
richtig. Unser Hook schiebt aber alles dahinter um X'24'. `BE` landete dadurch
mitten in einem BNH, und EXEC endete mit **S0C1**, sobald eine CLIST einen
Kommentar in Zeile 1 hatte (JOB01245). Die Gegenprobe mit dem IBM-EXEC lief
sauber (JOB01246). Ersetzt durch `@CF00059` und `@RF00406`. Mit derselben
Ersetzung assembliert die **unveränderte** Quelle zu einem byte-gleichen
Objekt, damit ist gezeigt, dass die Symbole stimmen. Weitere feste
Displacements gibt es in der Datei nicht. mvs38src ist informiert.

**Tests:** `tso/lab/exec_test.py`, Batch-TMP mit
`STEPLIB=REXX370.TSO.LINKLIB`. Das ist eine APF-Bibliothek außerhalb der
Link-Liste (IEAAPF00, MVS000). Beide Module werden mit
`tso/lmod_link.py testlib exec|ikjeft01` so gebunden, wie SMP es tut.

| Fall | `new-tmp` (JOB01253) | `ibm-tmp` (JOB01255) |
|---|---|---|
| `%RXA` SYSEXEC | REXX | COMMAND NOT FOUND (CLIST sucht nur SYSPROC) |
| `RXB` SYSPROC `/* REXX */` | REXX | als CLIST, `SAY NOT FOUND` |
| `%RXC` CLIST | CLIST | CLIST |
| `%RXD` CLIST, Kommentar in Zeile 1 | CLIST | CLIST |
| `%RXZZ` | not found | not found |
| `EXEC '…(RXC)'` explizit | CLIST | CLIST |
| `TIME` danach | läuft | läuft |

`ibm-tmp` ist die Übergangszeit nach dem APPLY und vor dem CLPA-IPL: neues
EXEC unter altem TMP. Die Ausgabe gleicht Zeile für Zeile dem IBM-EXEC.

**Der Hook greift nur beim impliziten Aufruf** (`CBUFOFF=0`). Ein explizites
`EXEC 'dsn(member)'` geht über IKJPARS und nie an IKJCT437 vorbei. Deshalb muss
das Test-EXEC unter seinem echten Namen gefunden werden, und ein Treiber unter
anderem Namen testet nichts.

**Offen:**
- Der ZMG-Usermod (++MOD-Decks + ++JCLIN für die neuen Module, `++VER
  FMID(EBB1102) PRE(UY16532,UY13431)`), danach der Test im Vordergrund.
- Teil B Phase 3: die expliziten `EXEC`-Formen.
- In `SYS2.LINKLIB` liegt IRXLDTSO aus `a59bd0c`, **ohne** #231. Vor dem
  Einspielen ein Compress, siehe `TODO.md`.

## Stand 2026-09-25 (Vormittag): der Klassifizierer läuft durch

IKJCT437 lädt jetzt über die **eigene Exec-Load-Routine der Umgebung**
(IRXEXTE `load_routine`, bei IRXTSPRM = IRXLDTSO mit BPAM, rexx370 #230),
nicht mehr per `LOAD EP=IRXLOAD`. Aufruf wie SC28-1883-0 Kap. 16: R0 =
ENVBLOCK, drei Parameter. Keine Load-Routine → zurück an CLIST. IRXEXEC
bekommt das geprüfte ENVBLOCK in P9 und R0.

Gemessen mit `tso/rxdrv_test.py run` (Batch-TMP, `JOB01214`) und im
Vordergrund (s3270 als MVSCE01, SYSEXEC/SYSPROC per ALLOC):

| Member | IKJCT437 | Ausgabe |
|---|---|---|
| `RXA` (SYSEXEC, ohne Kommentar) | REXX (R15=4) | `HELLO FROM SYSEXEC RXA -- NO COMMENT NEEDED` |
| `RXB` (SYSPROC, `/* REXX */`) | REXX (R15=4) | `HELLO FROM SYSPROC RXB` |
| `RXC` (SYSPROC, CLIST) | nicht REXX | — |
| `RXZZ` (nirgends) | nicht REXX, Ende nach C7 | — |

Damit ist die ganze Kette **ohne C-Runtime** belegt: TMP-Umgebung →
IKJCT437 → IRXLDTSO → IRXEXEC → IRXIOTSO (PUTLINE). `WHANDLED`,
`CHKLINE1` und das Aufräumen, bisher nur hergeleitet, sind jetzt gelaufen.

**Dabei gefunden: `CHKLINE1` benutzte R0 als Basisregister.** `CLC
0(2,R0),SLASHST` verglich PSA+0 mit `/*` — R0 als Basis heißt „keine
Basis". Jede SYSPROC-Exec galt als CLIST, ohne Absturz (`JOB01212`, RXB).
Behoben: R1 trägt die Adresse.

**Offen (Vormittag, inzwischen erledigt, siehe oben):**
- **IKJCT430:** der Aufruf ist drin, aber `BNZ RXHANDLD` springt auf die
  nächste Zeile — „war REXX, schon ausgeführt" läuft danach noch in den
  CLIST-Pfad. Das ist die Phase-1-Stelle, die jetzt fertig werden muss.
  IKJCT430 ist eine CSECT des Lademoduls **`SYS1.CMDLIB(EXEC)`** (Alias
  `EX`, dazu IKJCT431/432/435 und PARS), nicht der LPA: ein Ersatz dort
  braucht kein CLPA, nur dieselbe Extent-Vorsicht wie `SYS2.LINKLIB`
  (Link-List). Ob `EXEC` inzwischen komplett bindbar ist (IKJCT435, siehe
  `docs/REXX_TSO_INTEGRATION.md` „Bindung"), ist zu prüfen.
- In `SYS2.LINKLIB` liegt IRXLDTSO aus `a59bd0c`, also **ohne** das
  Entfernen der Satznummern (#231). Einspielen erst nach einem Compress —
  26 Tracks frei, siehe `TODO.md`.
- Teil B Phase 3: die expliziten `EXEC`-Formen.

---

## Stand 2026-09-24 (historisch)

Nach dem CLPA-IPL. Alles hier ist **gemessen**.
Wiedereinstieg: Abschnitt „Wo es weitergeht" am Ende.

---

## Wo wir stehen

`IKJCT437` läuft. Der Klassifizierer ist nicht der Blocker — er kommt bis
zum `IRXLOAD`-Aufruf durch, und dort bricht **IRXLOAD** ab, aus einem
Grund, der nichts mit diesem Modul zu tun hat.

Gemessener Pfad (Job `RXCT437 JOB01171`, Kommando `RXDRV RXA`):

```
+C1 ENTERED          CPPL da
+C2 ECT READ         ECT aus CPPL+12
+C3 ENVBLOCK READ    ECTENVBK
+C4 ENV EYE OK       'ENVBLOCK' stimmt -- Teil A traegt bis hierher
+C5 GETMAIN OK
+C6 EXECBLK BUILT
+C7 IRXLOAD LOADED
+__CRTGET CRT for TCB(009AB8B8) was not found in PPA(00000000)
+__CRTGET CRT for TCB(009AB8B8) was not found in PPA(00000000)
<S0C4, REASON 004>
```

Der Dump ordnet die Fehleradresse zu:

```
PSW AT ENTRY TO ABEND  078D0000 000ECC68   ILC 6   INTC 0004
  0009A070  RXDRV
  000E8A48  IRXLOAD     <-- 0xECC62 liegt hier
```

---

## Die Ursache: IRXLOAD braucht ein C-Runtime

`irx_load_load()` liest den Exec auf MVS mit **stdio**:

```c
memcpy(p, "DD:", 3);              /* "DD:SYSEXEC(MEMBER)" */
f = fopen(dd_spec, "r");
while (fgets(linebuf, sizeof(linebuf), f)) ...
```

`fopen` geht über `@@CRTGET`, das die C-Runtime-Fläche über die aktuelle
TCB in der PPA sucht. Findet es keine, liefert es NULL, und der nächste
Zugriff läuft in den NULL-GRT in Low Core — die Form, die
`src/irx#init.c` für Issue **#204** beschreibt. **Zwei** Meldungen, weil
`scan_member` zweimal läuft: `SYSEXEC`, dann `SYSPROC`.

`PPA(00000000)` heißt: im Adressraum existiert **kein** C-Runtime — die
PPA selbst ist NULL. Nicht „Runtime auf der falschen TCB", sondern gar
keins. Das ist Bauweise:

| Modul | `startup` | hat Runtime |
|---|---|---|
| `IRXJCL`, `IRXDBG`, alle `TST*` | `"crt1"` | ja — es sind C-Hosts |
| `IRXINIT`, `IRXTERM`, `IRXLOAD`, `IRXEXEC` | `false` | nein — sie erben das des Aufrufers |

Die `IRX*`-Dienste setzen einen C-Host voraus. httprexx ist einer,
`IRXJCL` ist einer. Bei der TSO-Integration ist keiner da: `IKJEFT01` und
`IKJCT430` sind IBM-Assembler.

**Warum `IKJEFTRX` trotzdem läuft:** `IRXINIT` ist gegen genau das
gehärtet (`crt_present_for_current_tcb()`, der #204-Schutz) und allokiert
über `irxstor` → `getmain`. `IRXLOAD` braucht eins, und niemand hat es je
aus Assembler heraus aufgerufen — `TEXECVL`, der grüne HLASM-Test,
übergibt einen **in-storage INSTBLK** und geht daran vorbei.

---

## Vollständiger Sweep: was auf dem MVS-Pfad ein Runtime braucht

Über **alle** Quellen der Module `IRXLOAD` und `IRXEXEC` plus lstring370,
jede Fundstelle dem Präprozessor-Zweig zugeordnet. Host-Zweige zählen
nicht.

### Braucht eins — drei Funktionen, neun Aufrufe

| Datei | Aufrufe | was es ist |
|---|---|---|
| `irx#load.c` `scan_member` | `fopen`/`fgets`/`fclose` | Exec-Member lesen |
| `irx#io.c` IRXINOUT | `fwrite`/`fputc`/`fflush` auf `stdout` | SAY |
| `irx#term.c` | `printf`/`fflush` | nur die `[bc] exec=/fallback=` Diagnosezeile |

### Braucht **keins** — jeweils an der Quelle geprüft

| Verdacht | Befund |
|---|---|
| ~30 × `sprintf`/`snprintf` in `bifs`/`pars`/`bctl` | `sprintf.c`, `snprintf.c`, `vsprintf.c`, `vsnprint.c` fassen weder CRT noch `errno` an; `@@examin.c` steht nicht in libc370s CRT-Liste |
| Zeit-BIFs | auf MVS `uclock64()` + `gmtime64_r()` — die **`_r`-Form ist CRT-frei**; nur das nicht-reentrante `gmtime64` ruft `__crtget()`, und das läuft im Host-Zweig |
| `irxstor` | `#ifdef __MVS__` kennt nur `getmain`/`freemain`; `calloc` steht ausschließlich im Host-Zweig |
| lstring370 | `malloc`/`free` sind nur der Default-Allokator — `irx#lstr.c` injiziert `irxstor` über `lstr_alloc`. `Lprint(FILE*)` wird nirgends gerufen |
| `rand`/`srand`/`strtok`/`getenv` | auf dem MVS-Pfad nicht vorhanden; `getenv` nur hinter dem #204-Schutz |

> **Korrektur zu `CLAUDE.md`:** dort steht, `irxstor` nehme ohne
> konfigurierten Subpool die crent370-Heap (`calloc`/`free`). Der
> MVS-Zweig kennt diesen Fall nicht.

---

## Die Umgebungen dürfen nicht vermischt werden

`IRXPARMS` (Batch), `IRXTSPRM` (TSO, TSOFL=1, Subpool 78) und `IRXISPRM`
(ISPF) tragen je eigene PARMBLOCK-Vorgaben;
`load_default_parmblock(is_tso(), …)` wählt zwischen den ersten beiden.
Was für eine Umgebung richtig ist, ist für die andere falsch —
insbesondere bei der I/O-Routine.

**`IRXJCL` ist ein reines Batch-Modul** (`EXEC PGM=IRXJCL,PARM=…`;
programmatischer CALL ist auf WP-CPS-08c vertagt). In reinem Batch ist
`ASXBLWA` NULL, `FINDENVB` Stage 1a läuft ins Leere, und `IRXPARMS`
greift — wie vorgesehen. Es taugt deshalb **nicht** als C-Host für den
TSO-Pfad: es würde `stdout` auf `DD:SYSTSPRT` umbiegen, also die
Batch-Annahme über eine TSO-Umgebung legen.

### Die TSO-I/O-Routine existiert noch gar nicht

> **Nachtrag 2026-09-25: erledigt, #228 / PR #229 (`b9ce45c`).** Es gibt jetzt
> `IRXIOTSO`. IRXINIT lädt die Routine über die MODNAMET von `IRXTSPRM`,
> nicht per `#ifdef`. Sie schreibt über **PUTLINE** und kommt ohne C-Runtime
> aus. Ein erster Anlauf mit TPUT ging im Batch-TMP stillschweigend verloren:
> SVC 93 tut ohne TSB nichts (KB `MVS-TSO-0001`, `PM-2026-004`). Gemessen:
> Hintergrund `SYSTSPRT` (JOB01189), Vordergrund am Terminal. Der
> „Future“-Kommentar in `irx#io.c`, der unten zitiert ist, ist korrigiert.
> Der Abschnitt bleibt als damaliger Befund stehen.

`irx#io.c` schreibt im eigenen Kopf:

> MVS (primary): irxinout — writes SAY/TRACE/error output to stdout +
> fflush. **irx_jcl_dispatch_main redirects stdout to DD:SYSTSPRT**
> before executing any exec …
> Future: **WP-33-TSO adds irxinout_tso (TPUT, TSO foreground)** with
> tso_flag-based dispatch at IRXINIT step 6.

Das heutige `irxinout` ist also die **Batch/IRXJCL-Routine**, und sie
funktioniert nur wegen dieser Umleitung (`irx#jcl.c:137`). Unter dem TMP
biegt das niemand um. `RXFREAD`/`RXFREADP` (PULL) liefern 20 — fehlt
ebenfalls (WP-33b).

Die `MODNAMET`-Slots für ersetzbare Routinen sind in **allen drei**
Parm-Modulen leer; die Auswahl passiert in C bei IRXINIT Schritt 6 per
`#ifdef __MVS__`. Dort gehört die `tso_flag`-Weiche hin.
*(2026-09-25: überholt. Die Weiche ist die MODNAMET selbst: `IRXTSPRM`
nennt `IRXIOTSO`, und IRXINIT lädt, was dort steht.)*

**WP-33-TSO ist damit keine Folge der Runtime-Frage, sondern ihre
Voraussetzung.** Auch mit perfektem C-Host hätte eine TSO-Umgebung heute
weder SAY noch PULL.

---

## Bewertung — was nach WP-33-TSO übrig bleibt

Wird `irxinout_tso` CRT-frei geschrieben (PUTLINE über einen
Assembler-Stub, wie `asm/istso.asm` es für `is_tso()` vormacht), dann
lösen WP-33-TSO und die Runtime-Frage sich mit demselben Stück Arbeit.
⚠️ libc370s eigenes `src/clib/tsocmd.c` steht in der CRT-Liste, taugt
dafür also nicht.

Dann bleibt als einzige CRT-Abhängigkeit des TSO-Pfads:

> **`scan_member` in `irx#load.c` — und damit ausschließlich `IRXLOAD`
> FC=LOAD.** FC=FREE ruft `scan_member` nicht und kommt ohne Runtime aus.

Zwei Wege von dort, beide klein genug für eine echte Wahl:

**Nachbauen ohne stdio.** `fopen("DD:dd(member)")` erledigt in libc370
echte Arbeit — Namen zerlegen, Member lokalisieren, öffnen — und `fgets`
entblockt Sätze für RECFM F/FB/V/VB. `osio.h` exportiert **19 Funktionen
und weder FIND noch POINT noch NOTE**. Nachbauen heißt DCB, OPEN,
`__bldl()` (in `clibos.h` vorhanden), POINT auf die TTR, Blöcke selbst
entblocken, je RECFM. Die subtile Sorte Code — und ein natürlicher
Kandidat, gleich in Assembler zu entstehen statt zweimal geschrieben zu
werden.

**Oder ein sehr kleiner C-Host**, der *nur* den Ladeschritt kapselt und
den INSTBLK zurückgibt (`getmain`-Speicher, überlebt dessen Abbau).
`IRXEXEC` liefe danach CRT-frei direkt aus `IKJCT437`. Kein `stdout`,
keine Umleitung, keine Vermischung der Umgebungen.

---

## Was an IKJCT437 in diesem Durchgang repariert wurde

**Offsets um ein Feld verschoben.** `struct instblk` hat bei +12 ein
`_filler1`. Die Summe der Felder ergibt genau die dokumentierten 128 Byte
Header — die Gegenprobe, die es festnagelt:

| Feld | las | ist |
|---|---|---|
| `instblk_address` | +12 | **+16** |
| `instblk_usedlen` | +16 | **+20** |
| `instblk_ddname`  | +28 | **+32** |

Mit +28 verglich die Sprachentscheidung die letzten vier Bytes des
*Membernamens* gegen `SYSEXEC` — das passt nie, jeder Exec wäre als CLIST
durchgereicht worden. Ein Fehler, der nichts abstürzen lässt.

**`WHANDLED` war nie initialisiert.** GETMAIN-Speicher ist nicht genullt.
`IC R2,WHANDLED` las Müll, das Modul konnte R15=4 melden („war REXX,
schon ausgeführt"), ohne dass etwas lief — und `IKJCT430` hätte das
Member lautlos geschluckt. Jetzt wird die ganze Fläche per `MVCL` genullt.

**`CHKLINE1` lief über das Ende.** `S R2,=F'4'` + `BM` ließ Satzlänge 4
mit R2=0 durch, `BCT` machte daraus X'FFFFFFFF'. Jetzt `S R2,=F'3'` +
`BNP` — die Zahl der Startpositionen für einen 4-Byte-Vergleich ist len-3.

Dazu: `IRXLOAD` wird nur noch **einmal** geladen (der FREE-Pfad lud es ein
zweites Mal, ohne DELETE), und beide Module werden vor dem FREEMAIN per
`DELETE` zurückgegeben.

⚠️ **Nur der Offset-Fehler ist belegt.** `WHANDLED`, `CHKLINE1` und die
DELETE-Hygiene liegen hinter der Stelle, an der der Lauf heute abbricht —
aus dem Code hergeleitet korrekt, aber von keinem Lauf bestätigt. Erst
wenn `IRXLOAD` durchkommt, sind sie gemessen.

---

## Fallen, die je einen Durchgang gekostet haben

🔴 **`RXDRV` muss als TSO-KOMMANDO laufen, nicht per `CALL`.** `CALL`
übergibt eine gewöhnliche PARM-Liste (R1 → Fullword → Halbwort-Länge +
Text), **keine CPPL**. So gerufen liest der Klassifizierer eine Müll-ECT,
verfehlt den Eyecatcher und antwortet „nicht REXX" — ohne abzustürzen.
Ein Lauf, der wie eine Messung aussieht und keine ist. Richtig:

```
//SYSTSIN DD *
 RXDRV RXA
```

🔴 **Ein RENT-Modul aus einer APF-Bibliothek liegt in Key 0.** MVS lädt es
nach Subpool 252; ein Key-8-Programm darf dort nicht schreiben. `RXDRV`
(mit Savearea im CSECT) starb deshalb bei `ST R13,4(,R3)` an **EPA+0x0C**,
noch vor dem ersten WTO — S0C4 Reason **004**. Gelöst mit
`--norent --noreus` beim Binden. `IKJCT437` ist davon nicht betroffen:
jeder seiner Stores geht auf `D(R13)`, die GETMAIN-Fläche, und die
WTO-Standardform ist `BAL` um den Text plus `SVC 35`, ohne Store. Gegen
das Listing geprüft, nicht behauptet.

🔴 **Reason Code 004 ist Schutz, nicht „ungültige Adresse".** Beide
Abstürze in diesem Durchgang waren 0C4/004, mit völlig verschiedenen
Ursachen. Der Reason Code trennt sie, der Abend-Code nicht.

🔴 **Die Probe-WTOs erscheinen als `+C1 ENTERED`** — mit führendem
Pluszeichen. Ein Filter auf `" C1 "` findet sie nicht und meldet einen
leeren Pfad, als wäre nichts gelaufen. Kostete einen Durchgang und einen
falschen Verdacht.

---

## Wo es weitergeht

1. ~~**WP-33-TSO** — `irxinout_tso` über einen PUTLINE-Stub, CRT-frei.~~
   **Erledigt** 2026-09-25, #228 / PR #229: `IRXIOTSO` wird über die MODNAMET
   geladen und schreibt über PUTLINE. Seit `JOB01194` steht es in
   `SYS2.LINKLIB`. Die Symbole sind CRT-frei, **gelaufen** ist die Routine
   ohne C-Runtime aber noch nicht, das kommt erst mit dem Weg über `IKJCT437`.
2. Danach `scan_member` — nachbauen oder kleiner C-Host, siehe Bewertung.
3. Dann erst ist der C1..C9-Pfad zu Ende messbar, und `WHANDLED` /
   `CHKLINE1` / DELETE werden von „hergeleitet" zu „belegt".
4. Offen bleibt Teil B Phase 3: die expliziten `EXEC`-Formen
   (`EX`-Keyword, `.EXEC`-Suffix neben `.CLIST` bei `IKJCT430:2174`).

### Wiederherstellen

```sh
# assemblieren (as370-Aufruf siehe TODO_IKJEFT01.md)
eval $ASM -o /tmp/IKJCT437.o tso/IKJCT437.ASM
eval $ASM -o /tmp/RXDRV.o    tso/RXDRV.ASM

# binden -- --norent ist Pflicht, siehe Fallen
ld370 -o dist_ptf/RXDRV.lm --name RXDRV --entry RXDRV --blocksize 19069 \
      --norent --noreus /tmp/RXDRV.o /tmp/IKJCT437.o -iebcopy
ld370 --pack RXDRV=dist_ptf/RXDRV.lm.iebcopy -o dist_ptf/RXDRV.xmit \
      --blocksize 19069 --norent --noreus -xmit
```

Testdaten auf `mvsdev.lan:8082` (Job `RXSETUP JOB01168`):

| Member | Bibliothek | erwartet |
|---|---|---|
| `RXA` | `IBMUSER.RXT.EXEC` (SYSEXEC) | REXX, ohne Inhaltsprüfung |
| `RXB` | `IBMUSER.RXT.PROC` (SYSPROC), `/* REXX */` | REXX |
| `RXC` | `IBMUSER.RXT.PROC`, CLIST | nicht REXX |
| `RXZZ` | nirgends | nicht REXX |

`SYS2.LINKLIB` ist APF-autorisiert und steht als zweiter Eintrag in
`LNKLST00`. Testjob-Vorlage: `/tmp/rxrun.py`.
