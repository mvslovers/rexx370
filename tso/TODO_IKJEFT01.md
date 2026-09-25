# IKJEFT01 — REXX/370 Environment beim TMP-Start

Arbeitsstand Teil A: `IRXTMPW` (Wrapper-TMP) wird ersetzt durch ein CSECT
`IKJEFTRX`, das aus `IKJEFT01` per `BAL` gerufen wird.

## Stand 2026-09-25: Auslieferung über SMP, nicht per ld370

Der TMP wird **nicht mehr als Ganzes gebaut**. Ausgeliefert werden nur die
Decks IKJEFT01 und IKJEFTRX, und SMP bindet sie gegen das installierte
`SYS1.LPALIB(IKJEFT01)` (KB `MVS-SMP-0004`). Das `ld370`-Rezept unten
beschreibt den Handbau vom 2026-09-23. Er lief, hat aber einen Fehler: er nahm
IKJEFT06 und IKJEFTSC aus mvs38src (TK5-Stand), und der Alias IKJEFT0A blieb
auf dem alten Modul stehen.

- **Basis ist der TK5-Stand.** Die mvs38src-Quelle von IKJEFT01 ist
  byte-gleich mit `SYS1.AOST4(IKJEFT01)` = UY13431 (JOB01225). Gebunden wie SMP
  bindet ist sie byte-gleich mit dem installierten LMOD (JOB01240).
- **MVSCE-LAB war nicht auf diesem Stand.** Nach dem Tausch der MVSRES-Platte
  trug LPALIB IKJEFT01 auf UZ82014, obwohl das Inventar UY13431 führte.
  Repariert am 2026-09-25: IKJEFT01 aus AOST4 neu gebunden (JOB01229), APPLY und
  ACCEPT von UY43678 (IKJEFTSC) und UZ42826 (IKJEFT06, steckt auch in
  IKJEFT02/04/07), ACCEPT von UY16532 (JOB01231–01235). Danach IPL mit CLPA.
  Die Sicherung liegt unter `<HLQ>.TSOREP.*`, die Werkzeuge in `tso/lab/`.
- **Die Link-Steuerung kommt aus der CDS:** `ORDER IKJEFT01(P),IKJEFT06`,
  `ALIAS IKJEFT0A`, `ENTRY IKJEFT01`, `SETCODE AC(1)`, RENT REUS.
- **Test ohne Eingriff in SYS1:** `tso/lmod_link.py testlib ikjeft01` bindet den
  gepatchten TMP nach `REXX370.TSO.LINKLIB`. Ein Batch-TMP mit diesem STEPLIB
  nimmt ihn noch vor der LPA (`tso/lab/exec_test.py new-tmp`).

Stand 2026-09-23. Alles darunter ist **gemessen**, nicht angenommen.

---

## Dateien hier

| Datei | Was |
|---|---|
| `IKJEFT01.ASM` | Kopie aus `mvs38src@33c6bd57` + 10 eingefügte Karten |
| ~~`IKJEFT01.ASM.orig`~~ | entfernt; die Referenz wird aus `mvs38src/src/IKJEFT01.ASM` assembliert (`build.sh`) |
| `IKJEFTRX.ASM` | unser neues CSECT |

`mvs38src/src/` wird **nicht** verändert — dort wird mehrmals pro Stunde
committet. Immer mit einer Kopie arbeiten.

---

## Der Eingriff

Zehn Karten nach `SPKA 0(@03)` am Konvergenzpunkt `@RF00530` (Zeile 1053 der
Kopie), davon drei Code:

```
         L     15,=V(IKJEFTRX)
         BALR  14,15
         CNOP  0,4
```

**Warum dort:** `@RF00530` ist der Punkt, an dem Vordergrund- und
Hintergrundpfad zusammenlaufen (der `BNZ` bei 1036 testet `BKGMODE`). Code hier
läuft also auch im Batch-TMP — genau das, was `IRXTMPW` nie erreichte. Und
TK5s `SPKA` davor hat uns bereits in den `SPROK1`-Key gebracht, wir brauchen
keine eigene Key-Klammer.

⚠️ **TK5 nimmt hier `SPROK1`, nicht `TEPKEY`.** Ältere Pläne nennen `TEPKEY`;
das wäre der falsche Key.

⚠️ **Das `CNOP 0,4` ist nicht Kosmetik.** `L`+`BALR` sind 6 Bytes, und
6 mod 4 = 2 — ohne `CNOP` verschiebt sich alles dahinter um zwei Bytes und
`O 15,TMIQCON` (Zeile 1295) verliert seine Fullword-Grenze. Der Assembler
meldet das nur als Severity-4-Warnung; auf der Maschine wäre es eine
Spezifikationsausnahme.

---

## Bauen

Assembler ist der **gepinnte** `as370` aus `mvs38src`, mit zwölf `-I`-Pfaden.
`ASMDATE`/`ASMTIME` sind **Umgebungsvariablen, keine Optionen**. `IKJEFT01`,
`IKJEFT06` und `IKJEFTSC` stehen in keiner der Parametertabellen
(`macrolevel.tsv`, `asmdate.tsv`, `sysparm.tsv`), bauen also auf Vorgaben.

```sh
cd ~/repos/mvs/mvs38src
ASM="ASMDATE=09/07/26 ASMTIME=12.00 work/src-states/bin/as370-main \
  -I work/macros/tk5-recon -I work/macros/pls-header \
  -I work/macros/mvsce-2.1.4-dlib/AMACLIB -I work/macros/mvsce-2.1.4-dlib/AMODGEN \
  -I work/macros/mvsce-2.1.4-dlib/AGENLIB -I work/macros/mvsce-2.1.4-dlib/ATSOMAC \
  -I work/macros/mvsce-2.1.4-dlib/ATCAMMAC -I work/macros/mvsce-2.1.4-dlib/APVTMACS \
  -I work/macros/tape -I work/macros/mirror -I work/macros/erep-set \
  -I work/macros/amaclib-live"

# unsere beiden
eval $ASM -o /tmp/ikjeft01_patched.o ~/repos/mvs/rexx370/tso/IKJEFT01.ASM
eval $ASM -o /tmp/IKJEFTRX.o         ~/repos/mvs/rexx370/tso/IKJEFTRX.ASM
# die zwei Begleiter aus mvs38src
eval $ASM -o /tmp/IKJEFT06.o src/IKJEFT06.ASM
eval $ASM -o /tmp/IKJEFTSC.o src/IKJEFTSC.ASM
```

Alle vier: **Exit 0, keine Diagnostik.**

🔴 **`as370` schreibt bei Severity 8 trotzdem ein Objekt und kann mit 0 enden.**
Nie prüfen, ob die `.o` existiert — immer Rückgabecode *und* Diagnostikzeilen
lesen. Und beim Messen des Rückgabecodes nicht durch `| head` pipen, das meldet
head's Status.

---

## Binden

```sh
ld370 -o IKJEFT01.lm --name IKJEFT01 --entry IKJEFT01 --ac 1 \
      /tmp/ikjeft01_patched.o /tmp/IKJEFT06.o /tmp/IKJEFTSC.o /tmp/IKJEFTRX.o \
      -iebcopy
```

🔴 **`--ac 1` ist Pflicht.** Ohne AC(1) endet der TMP mit **S047** (`ILC 2
INTC 006B` = SVC 107 = MODESET, ein autorisierungspflichtiger Dienst, den
`IKJEFT01` selbst absetzt). Gemessen 2026-09-23. Das Zielbibliothek muss
APF-autorisiert sein, sonst ist AC(1) wirkungslos — `SYS2.LINKLIB` auf
MVS/CE-LAB ist es.

**Genau diese vier, kein weiteres Modul.** Der ESD von `IKJEFT01` nennt sieben
externe Verweise, die sich aber gegenseitig decken:

| gebraucht | geliefert von |
|---|---|
| `RDYMSG`, `TM03SL`, `TM04SL` | `IKJEFT06` (25 `LD`-Einträge) |
| `IKJEFTSC`, `IKJRECPT`, `IKJBETSC` | `IKJEFTSC` |
| `IKJEFWAI` (braucht `IKJEFTSC`) | `IKJEFT01` selbst |
| `IKJEFTRX` | uns |

---

## Belegt

**Die Referenz wird reproduziert.** Ohne unseren Patch gebunden:

```
IKJEFT01 len=001BB8 | IKJEFT06 len=0006A0 | IKJEFTSC len=000ED0
```

— dieselben Längen, die das `mvs38src`-Team aus dem echten
`SYS1.LPALIB(IKJEFT01)` von TK5 gemessen hat. Die Kette Quelle → Objekt →
Lademodul stimmt also, bevor wir etwas Eigenes einbringen.

**Mit Patch:**

```
IKJEFT01 len=001BC4  (+12: 6 Code + 6 CNOP-Padding)
IKJEFT06 len=0006A0  unverändert
IKJEFTSC len=000ED0  unverändert
IKJEFTRX len=000140  neu
```

Objektseitig +1 externer Verweis und +1 RLD-Karte — beides unser `V`-Con.
Die `LD`-Einträge `IKJEFT0A`/`IKJEFWAI` sind mitgewandert und intakt.

**`IKJEFTRX` selbst:** assembliert und bindet einzeln, und die Basisregister
stimmen im Objektcode (`90ecd00c` / `98ecd00c` = `STM`/`LM … 12(13)`, nicht
Basis 0).

---

## BLDL — REXX/370 nur anfassen, wenn es installiert ist

`IKJEFTRX` fragt per BLDL, ob `IRXANCHR` und `IRXINIT` überhaupt da sind,
bevor es lädt. Ohne das würde jeder Logon auf einem System ohne REXX/370
zweimal in einen `LOAD`-ERRET-Pfad laufen — das ist ein Recovery-Einstieg,
keine Frage.

Zwei Dinge daran haben je einen Durchgang gekostet, beide gemessen:

**Eintragslänge 58, nicht 12.** 8 Byte Name + 50 Byte, die BLDL füllt — die
Größe, die IBMs eigene Aufrufer nehmen (`IKJEBECI:798`).

**Symbolische Form, nicht Registernotation.**

```
         BLDL  0,WBLDL      richtig   -- findet
         BLDL  0,(1)        falsch    -- RC=4, auch fuer IEFBR14
```

`BLDL 0,(1)` mit der Listenadresse bereits in R1 assembliert anstandslos,
findet aber **nichts** — RC=4 selbst für `IEFBR14` und `IKJEFT01`, die
garantiert in `SYS1.LINKLIB` stehen. IBM schreibt `BLDL 0,BLDLIST`
(`IKJEBECI:320`); das tut es auch.

> Gefunden wurde das nur durch eine Kontrollprobe mit garantiert vorhandenen
> Namen. Die trennte „Suchpfad" von „Listenformat" und zeigte auf Letzteres.
> Ohne sie hätte man beliebig lange gesucht, ob `SYS2.LINKLIB` in der
> LINKLIST steht — steht es (`LNKLST00`, zweiter Eintrag), war aber nie das
> Problem.

**Folge für den Aufbau:** der `GETMAIN` muss **vor** die `LOAD`s, weil BLDL
seine Ergebnisse in die Liste schreibt und das Modul RENT ist — die Liste
kann nicht statisch sein. Die `LOAD ... ERRET=` bleiben trotzdem: BLDL sagt
„ist da", nicht „lädt sich auch".

---

## Belegte Läufe (MVS/CE-LAB, mvsdev.lan:8082)

`IKJEFT42` = dieses Lademodul unter anderem Namen, in `SYS2.LINKLIB`,
getestet mit `jcl/RXTMPTST.jcl` (`PGM=IKJEFT42`, darin
`CALL 'SYS2.LINKLIB(IRXDBG)' 'ANCH'`).

| Lauf | Ergebnis |
|---|---|
| ohne Patch (Kontrolle) | `CC 0000`, `USED: 0`, `Active entries: 0` |
| mit Patch, ohne `--ac 1` | **S047** (MODESET) |
| mit Patch, AC(1), ohne Key-Klammer | **S0C4** beim ersten `STM` |
| mit Patch, AC(1), TEPKEY-Klammer | **`CC 0000`** |

Der letzte Lauf (`JOB01133`):

```
IKJ56942I REXX/370 TSO ENVIRONMENT INITIALIZED
USED: 2     Active entries: 1 / Free slots: 62
Slot 1 | envblock_ptr 9BD80 | token 1 | tcb_ptr 9965D8 | flags 40000000
```

---

## Meldungsnummer

**`IKJ56942I`**. `IKJ56420I` war der naheliegende Kandidat, ist aber belegt —
*„USERID NOT AUTHORIZED TO USE TSO"* in `IKJEFLGN:684`. Von 574 im IKJ-Baum
belegten Nummern blieben im 56xxx-Bereich sieben freie mit einer 42;
`IKJ569xx` ist mit 5 belegten Nummern der ruhigste Block.

---

## Werkzeug: Upload ohne Homebrew-Python

macOS sperrt Homebrew-Python zeitweise vom lokalen Netz aus (`EHOSTUNREACH`,
während `curl` durchkommt). `/usr/bin/python3` ist 3.9 und damit zu alt für
`mbt.mvsmf` (`str | None`). Ausweg: ein Minimal-Client mit reinem `urllib`,
3.9-tauglich — `/tmp/mvs39.py`, Bau-Test-Schleife in `/tmp/cycle.py`.

---

## Nicht belegt

- **Dass `R13` an der Einbaustelle auf eine benutzbare Savearea zeigt.** Indiz:
  TK5s eigener Block bei `TMIP8` macht dort `STM @14,@12,12(@13)`. Ein Indiz,
  kein Beweis.
- **Dass `IRXINIT` in dieser Umgebung durchläuft.**
- Beides klärt erst ein Lauf auf der Maschine.

---

## Logon bestätigt (2026-09-24, MVS/CE-LAB nach IPL mit CLPA)

`SYS1.LPALIB(IKJEFT01)` ersetzt, IPL gefahren, Vordergrund-Logon:

```
IEF125I IBMUSER - LOGGED ON - TIME=03.21.56
IKJ56942I REXX/370 TSO ENVIRONMENT INITIALIZED
```

Aus TSO READY, alle drei Sichten konsistent:

```
irxdbg anch   Slot 1 | envblock 99E50 | token 1 | tcb 9DC400 | flags 40000000
              USED: 2   Active entries: 1 / Free slots: 62

irxdbg ect    Current TCB (PSATOLD):  009D56A8
              Current ECT:            00097CF4
              ECTENVBK (ECT+0x30):    00099E50   <- zeigt auf die ENVBLOCK
              ENVBLOCK: Eye-catcher gueltig, Version '0042', Laenge 320

irxdbg env    parmblock 00099DD0 | workblkext 00099710 | irxexte 00099D50
              ectptr    00097CF4  <- zeigt auf dieselbe ECT zurueck
              userfield 00000000  <- frei fuer eingebettete Aufrufer
```

**Der TCB im Ankerslot (`9DC400`) ist NICHT der aktuelle TCB (`9D56A8`) — und
das ist richtig so.** `IRXINIT` lief unter dem TMP-TCB während der
Initialisierung; das `irxdbg`-Kommando läuft unter einem
Kommando-Subtask-TCB. Dass die Environment trotzdem gefunden wird, ist genau
der Grund, warum sie in der **ECT** verankert ist und nicht im TCB: die ECT ist
pro Session, nicht pro Task. Und dass `IRXANCHR` per `LOAD` in die Step-TCB-JPQ
gelegt wird, ist der Grund, warum der Subtask es überhaupt findet.

Die Rückverkettung schließt sich: Anker → ENVBLOCK `99E50`, ENVBLOCK `ectptr`
→ ECT `97CF4`, ECT+0x30 → zurück auf `99E50`.

---

## Offen
2. **`IKJ56942I` pro Logon** — für den Bring-up gewollt, auf Dauer vermutlich
   zu laut. Entscheiden, ob sie bleibt.
3. **`IRXANCHR`/`IRXINIT` müssen in der Link-Library liegen**, nicht nur in
   einer STEPLIB: BLDL mit DCB=0 durchsucht die Link-Library, und beim Logon
   gibt es keine STEPLIB. Auf dem LAB steht `SYS2.LINKLIB` in `LNKLST00`.
   **Stand 2026-09-25:** `SYS2.LINKLIB` trägt den rexx370-Satz aus `b9ce45c`
   (`JOB01194`): IRX#HELO, IRXANCHR, IRXDBG, IRXEXEC, IRXINIT, IRXISPRM,
   IRXJCL, IRXLOAD, IRXPARMS, IRXTERM, IRXTSPRM und neu **`IRXIOTSO`**. Der
   Stand davor ist in `IBMUSER.SYS2LINK.BKUP0925` gesichert. Die Datei hat
   weiterhin ein Extent und ist zu 80 % belegt (118 von 600 Tracks frei). Jedes
   IEBCOPY-Replace hinterlässt toten Platz, und eine Neuordnung einer
   Link-List-Datei im laufenden Betrieb ist riskant. Irgendwann ist deshalb
   ein Compress fällig, am besten direkt vor einem IPL. Geprüft per
   Vordergrund-Logon (MVSCE01), bei dem `IRXIOTSO` nur aus der Link-List kam.
4. **Leerer `(blank) PC len=000000`** aus unserem Objekt landet im CESD des
   Lademoduls; das Original hat ihn nicht. Folgenlos (Länge null), aber
   unsauber. Entsteht durch Statements vor der `CSECT`-Karte.
5. **Modulname** `IKJEFTRX` ist vorläufig. `IKJEFT0A/0B/0D/0E` sind in z/OS
   bereits Aliase von `IKJEFT01`/`02`/`04`.

---

## Fallen, die uns schon erwischt haben

- **Spalte 72** ist die Fortsetzungsspalte. Marker gehört wie bei TK5 auf
  Spalte 65, nicht weiter rechts. Sätze sind 80 Zeichen mit CRLF.
- **`CNOP`** nach eingefügtem Code, sonst Ausrichtung kaputt (siehe oben).
- **RS-Format** (`LM`/`STM`) braucht `D(B)`; `D(,B)` ist seit 2026-09-23 ein
  `as370`-Fehler mit Exit 12, der den Build stoppt — früher wurde es still mit
  Basis 0 assembliert.
- **RX-Format umgekehrt**: `SYM(@15)` ist die *Index*form mit Basis 0; die
  Basisform braucht das Komma, `SYM(,@15)`.
- **`EQU`, dessen Ausdruck ein später definiertes `EQU` nennt**, löst still auf
  null auf — kein Diagnostikum. (`mvs38src/tools/unrefd.py --forward`,
  `tools/basezero.py` finden beides.)
