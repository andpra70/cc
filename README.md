# cc - Tiny C Compiler Self-Hosted

Progetto sperimentale di compilatore C minimale con backend multipli (ELF, ASM, AST/LLVM-like, interprete) e pipeline di self-hosting.

Obiettivo pratico:
- compilare sorgenti C semplici,
- generare un ELF eseguibile direttamente,
- eseguire catena di bootstrap `cc -> cc.elf -> cc.elf2` in modo stabile.

## Struttura repository

- `src/`
- `src/cc.c`: driver CLI del compilatore.
- `src/ast.c`: lexer, parser, simboli/tipi, AST.
- `src/g_elf.c`: codegen ELF x86_64.
- `src/g_asm.c`: emissione assembly testuale.
- `src/g_ast.c`: emissione AST intermedia (llvm/simbolica).
- `src/g_interpreter.c`: interprete AST.
- `src/cpu.c`: runner per output `.llvm`.
- `src/minilib.c`: include wrapper verso `lib/minilib.c`.
- `include/`: header minimi locali (`stdio.h`, `stdlib.h`, `string.h`, ecc.).
- `lib/`: implementazioni C minimali di supporto.
- `Makefile`: target build/test/self.
- `test0.sh`: script di test rapido.

## Build

Prerequisiti host minimi:
- compilatore C host (`cc`/`gcc`/`clang`) per il primo bootstrap,
- ambiente Linux x86_64.

Comandi:

```bash
make cc
make cpu
make all
```

Pulizia:

```bash
make clean
```

## Configurazione Memoria/Footprint

I parametri principali sono in:

- `src/config.h`

Modifica i `#define` iniziali e ricompila (`make clean && make cc`).

Parametri disponibili:
- `CC_CFG_IO_BUFFER_INIT`: dimensione iniziale buffer lettura file/stdin.
- `CC_CFG_PP_SRC_STACK_MAX`: profondità massima stack include/preprocessor.
- `CC_CFG_ELF_CODE_INIT`: chunk iniziale buffer binario codegen ELF.
- `CC_CFG_ELF_LABELS_INIT`: capacità iniziale tabella label ELF.
- `CC_CFG_ELF_FIXUPS_INIT`: capacità iniziale tabella fixup ELF.
- `CC_CFG_ELF_FUNC_SLOT_BYTES`: spazio per metadati funzione nel pass ELF.
- `CC_CFG_ELF_LOOP_NEST_MAX`: nesting massimo loop/switch (`break/continue` stack).
- `CC_CFG_ELF_STR_LIT_MAX`: numero massimo string literal uniche per unità compilata.
- `CC_CFG_CALL_ARG_GUARD_MAX`: guardia anti-ciclo/overflow su liste argomenti call.

Indicazioni pratiche:
- Se hai errori tipo `too many string literals`, alza `CC_CFG_ELF_STR_LIT_MAX`.
- Se hai codice con nesting molto profondo, alza `CC_CFG_ELF_LOOP_NEST_MAX`.
- Per ridurre RAM, abbassa i valori ma aspettati limiti più stretti su input grandi.
- Dopo ogni modifica, verifica sempre con `make self` e `make test`.

## Uso CLI

Sintassi:

```bash
./cc [opzioni] input.c
```

Opzioni:
- `-o <file>`: nome file output.
- `-l <dir>`: directory output (relativa alla cwd).
- `-s`: emette `.asm`.
- `-a`: emette `.llvm` (AST-like textual IR del progetto).
- `-i`: esecuzione via interprete AST.
- `-v`: log verbose parsing/codegen.

Default senza `-s/-a/-i`: output ELF eseguibile (`.elf`).

Esempi:

```bash
./cc src/test.c -o test.elf
./test.elf

./cc src/test.c -a -o test.llvm
./cpu test.llvm

./cc src/test.c -s -o test.asm
```

## Self-hosting

Flusso atteso:

1. build host del primo compilatore (`cc`),
2. compilazione di `src/cc.c` in `cc.elf`,
3. compilazione di `src/cc.c` con `cc.elf` in `cc.elf2`,
4. uso di `cc.elf2` per compilare ed eseguire test.

Target dedicato:

```bash
make self
```

## Test

Target principale:

```bash
make test
```

Esegue:
- test percorso `.llvm` con `cpu`,
- test percorso `.elf` nativo,
- validazione funzionale su `src/test.c` (math, logic, if/while/for/switch, memory, pointer, struct/union, calls, recursion, bit ops, ecc.).

Output atteso finale:
- `total_fail:0`

## Architettura (sintesi)

Pipeline:
1. Lexer + parser (`src/ast.c`)
2. AST
3. Backend selezionato:
- `g_elf.c` -> ELF x86_64 eseguibile,
- `g_asm.c` -> assembly,
- `g_ast.c` -> IR testuale,
- `g_interpreter.c` -> esecuzione interpretata.

Il backend ELF emette direttamente immagine ELF senza invocare assembler/linker esterni nella fase di compilazione target.

## Limiti del sistema

Limiti intenzionali/noti (progetto minimale):
- supporto C parziale (non full C99/C11).
- parser e type-system semplificati.
- supporto incompleto per dichiarazioni/forme sintattiche avanzate.
- ABI/calling convention copre il caso principale usato dai test, non tutti i corner-case.
- backend ELF orientato a Linux x86_64.
- runtime/libc locale minimale (non equivalente a libc completa).
- preprocessing ridotto (macro/include basilari, non pienamente compatibile con CPP standard).
- error reporting essenziale.

In generale va considerato un compilatore didattico/sperimentale, non un drop-in replacement di gcc/clang.

## Note progetto

- Il codice privilegia auto-consistenza e self-hosting.
- Le dipendenze standard sono minimizzate con header/lib locali.
- In caso di regressioni, verificare prima `make self` e poi `make test`.

## Header/lib locali (ANSI C + POSIX subset)

Gli header in `include/` espongono un subset minimale ma coerente di API ANSI C/POSIX usate dal progetto, con include guard e tipi base (`size_t`, `ssize_t`, `off_t`, `mode_t`, `intptr_t`, ecc.).

In particolare:
- ANSI C (subset): `stddef.h`, `stdint.h`, `limits.h`, `ctype.h`, `string.h`, `stdlib.h`, `stdio.h`, `math.h`.
- POSIX (subset): `unistd.h`, `fcntl.h`, `sys/types.h`, `sys/mman.h`.

Le implementazioni minimali restano in `lib/` e sono pensate per semplicità e self-hosting, non come sostituto completo di una libc di sistema.
