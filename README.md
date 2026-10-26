# Hands-on 2 · Laboratorio interactivo de analizadores léxicos (Lex)

Compiladores · Universidad de Guadalajara  
Alumno: Alondra Goretti Gonzalez Martinez
        Barraza Cervantes Emiliano

Simulador paso a paso de un analizador léxico al estilo **Lex/Flex** para un subconjunto de C. Muestra cómo **todas las reglas compiten desde la misma posición**, cómo se aplica la **máxima coincidencia** (y la prioridad por orden en caso de empate) y cómo se **ejecuta la acción** de la regla ganadora (`yytext`, `yyleng`).

Misma arquitectura que el Hands-on 1: **todo el motor está en C++** y la página solo lo dibuja. El mismo `.cpp` se compila a consola (`g++`) y a la web (`emcc` → WebAssembly).

## 1. Archivos

| Archivo | Función | Se publica |
|---|---|---|
| `lex_lab.cpp` | Motor completo: lector del archivo `.l`, expresiones regulares → AFN, simulación, JSON, CLI | recomendado |
| `lex_lab.wasm` | El motor compilado (≈ 71 KB) | **obligatorio** |
| `lex_lab.js` | Pegamento generado por Emscripten (carga e instancia el `.wasm`) | **obligatorio** |
| `index.html` | Interfaz: programa, reglas, acción y datos, resultado, editor del `.l` | **obligatorio** |
| `README.md` | Este documento | recomendado |

`index.html`, `lex_lab.js` y `lex_lab.wasm` deben estar **en la misma carpeta** y con esos nombres (el nombre del `.wasm` está escrito dentro del `.js`).

## 2. Cómo funciona

```
programa C ──► decodificar (UTF-8 → símbolos)
archivo .l ──► compilarEspec ──► por regla: patrón ──expandir {NOMBRE}──► ParserRx ──► AFN (Thompson)
                                                │
                       analizar: en cada posición, las N reglas avanzan en paralelo (un conjunto de estados por regla)
                       · tras cada carácter: reglas VIVAS y reglas que ACEPTAN
                       · fin del reconocimiento: ninguna regla puede continuar (o fin de la entrada)
                       · SELECCIÓN: mayor longitud; en empate, menor número de regla
                       · ACCIÓN: TOKEN / OMITIR / ERROR  (o regla por defecto de Lex si nada aceptó)
                                                │
                       jsonAnalisis ──► index.html (JSON.parse y dibujo; la página no reanaliza nada)
```

| Pieza del C++ | Qué hace |
|---|---|
| `SPEC_DEFECTO` | El archivo `.l` por defecto: 49 reglas en tres secciones separadas por `%%` |
| `ParserRx` | Analiza una expresión regular y construye el AFN (concatenación, `\|`, `*`, `+`, `?`, grupos, clases `[...]`/`[^...]`, `"literales"`, `.`, escapes) |
| `expandir` | Sustituye `{NOMBRE}` por la definición (entre paréntesis) |
| `compilarEspec` | Lee `%{ %}`, definiciones, reglas y acciones; reporta errores con número de línea |
| `cierreDe`, `avanzar`, `acepta` | Simulación del AFN (cierre ε, transición por símbolo, estado de aceptación) |
| `analizar` | El ciclo de Lex: reconocer → seleccionar → ejecutar acción; guarda cada paso |
| `jsonAnalisis`, `jsonEspec` | Salidas JSON para la web |
| `imprimir` / `main` | Salida de consola (solo con `g++`) |
| `lx_*` (`#ifdef __EMSCRIPTEN__`) | Interfaz exportada a JavaScript |

### Reglas por defecto (49)
Preprocesador (`#include`, `#define`), 13 palabras reservadas, identificadores, números (científicos, decimales, enteros), cadenas con escapes, comentarios de bloque y de línea, operadores y signos de puntuación, espacios y la regla `ERROR` (`.`).  
Ejemplos de la **máxima coincidencia** y la **prioridad**: `integer` es identificador (más largo que `int`); `int` es `KW_INT` (empata con `IDENTIFICADOR` y la regla 3 va antes que la 16); `1.25` es decimal y no `1` + `.` + `25`.

### Acciones soportadas
Las acciones en C **no se ejecutan**: se simulan tres operaciones definidas en el prólogo del `.l`.

| Acción | Efecto |
|---|---|
| `TOKEN("NOMBRE")` | Imprime el token con `yytext` y lo agrega al resultado |
| `OMITIR("NOMBRE")` | Descarta el lexema (espacios, comentarios) |
| `ERROR("NOMBRE")` | Informa un carácter no reconocido y lo consume |

Otra acción distinta produce un aviso y se trata como omisión. Si ninguna regla acepta, se aplica la **regla por defecto de Lex** (copia el carácter) y se cuenta como no reconocido.

## 3. Interfaz web

* **Programa de entrada**: 4 programas (Expresiones, Ciclo for, Ciclos y decisiones, Programa completo en C) o **Editar código** para escribir el tuyo.
* **Siguiente carácter / Anterior**: un paso del ciclo (fases 1 Reconocer · 2 Seleccionar · 3 Ejecutar acción). **Siguiente acción**, **Siguiente línea**, **Completar análisis** y **Reiniciar**. También las flechas ← y →.
* **01 Código fuente**: procesado / en reconocimiento / carácter examinado.
* **02 Reglas de Lex**: estado de cada regla (puede continuar, acepta, aceptó antes, descartada, ganadora, pierde por longitud o prioridad). *Solo candidatas* oculta las descartadas. Pulsar una regla muestra su patrón y acción.
* **03 Acción y datos**: `yytext`, `yyleng` y la regla con su acción.
* **04 Resultado**: tokens con posición `línea:columna`; *Mostrar omisiones*; **Exportar JSON**.
* **Ver archivo .l / Experimentar con las reglas**: ventana con tres pestañas (**Definiciones**, **Reglas y acciones**, **Código auxiliar**). Al experimentar se puede editar cualquier pestaña y **Aplicar reglas**: el motor C++ recompila los autómatas. **Descargar .l** entrega el archivo con sus secciones separadas por `%%`.

## 4. Interfaz C++ ↔ JavaScript (solo enteros, como el Hands-on 1)

| Función exportada | Qué hace |
|---|---|
| `lx_canal(c)` | Elige el búfer: 0 = programa C, 1 = archivo `.l` |
| `lx_limpiar()` / `lx_byte(b)` | Vacía / agrega un byte al búfer elegido |
| `lx_procesar(modo)` | 0 analiza el programa · 1 compila y activa el `.l` del canal 1 · 2 restaura las reglas originales · 3 devuelve el texto `.l` vigente. Devuelve la longitud del resultado |
| `lx_leer(i)` | Byte `i` del resultado (JSON o texto) |

Un `.l` con errores **no reemplaza** a las reglas vigentes; el mensaje indica la línea y el problema.

## 5. Compilar y probar

```bash
# Consola (misma lógica que la web)
g++ -std=c++17 -O2 lex_lab.cpp -o lex_lab
./lex_lab programa.c            # reporte de texto
./lex_lab --traza programa.c    # agrega la traza de cada lexema
./lex_lab --json programa.c     # el JSON que recibe la página
./lex_lab --spec                # imprime el archivo .l por defecto
./lex_lab --reglas mi.l programa.c

# WebAssembly
emcc lex_lab.cpp -std=c++17 -O2 -sALLOW_MEMORY_GROWTH=1 -o lex_lab.js    # genera lex_lab.js + lex_lab.wasm
python3 -m http.server 8000     # abrir http://localhost:8000/index.html  (no con doble clic: file:// bloquea el .wasm)
```

`-sALLOW_MEMORY_GROWTH=1` permite analizar programas grandes (el JSON de la traza crece con la entrada).  
El motor no usa excepciones de C++ (Emscripten las desactiva por defecto): los errores de patrones se devuelven como mensajes.

## 6. Publicar en GitHub Pages

```bash
git init
git add index.html lex_lab.js lex_lab.wasm lex_lab.cpp README.md
git commit -m "Hands-on 2: laboratorio de Lex"
git branch -M main
git remote add origin https://github.com/<usuario>/<repo>.git
git push -u origin main
```
En GitHub: **Settings → Pages → Deploy from a branch → `main` / `(root)`**. El sitio queda en `https://<usuario>.github.io/<repo>/`.  
Cada vez que cambies el `.cpp`, recompila y sube **los dos** archivos generados (`lex_lab.js` y `lex_lab.wasm`).

## 7. Cómo agregar o cambiar reglas

* **Sin recompilar**: abre *Experimentar con las reglas*, edita y *Aplicar reglas* (los cambios duran hasta recargar la página).
* **Para que sea permanente**: edita `SPEC_DEFECTO` en `lex_lab.cpp` (definiciones arriba, reglas entre los dos `%%`) y recompila.
* **Un token nuevo** (por ejemplo `"switch"`): agrega `"switch"  { TOKEN("KW_SWITCH"); }` **antes** de la regla de identificadores (menor número = mayor prioridad).
* **No hay que tocar `index.html`**: el listado de reglas, los estados y los tokens se construyen a partir del JSON del motor.

## 8. Pruebas realizadas

Con `g++` y con el `.wasm` ejecutado en Node, con los mismos resultados:

| Programa | Tokens | Omisiones | Errores |
|---|---|---|---|
| 01 Expresiones | 6 | 5 | 0 |
| 02 Ciclo for | 22 | 16 | 0 |
| 03 Ciclos y decisiones | 32 | 27 | 0 |
| 04 Programa completo en C | 61 | 42 | 0 |

Casos límite verificados: `integer`/`int`/`ifx`/`if`; `1.x`, `1e`, `1e+5`, `1.5e2.3`, `.5`; `a--b`, `x->y`, `a<<=2`; comentario sin cierre; cadena sin cierre; escape inválido (`"mal\q"`); caracteres no ASCII (`ñ`, `é`, emoji) dentro y fuera de cadenas; `@`, `$`, `` ` ``; archivos `.l` con corchete sin cerrar, definición inexistente, regla que acepta la cadena vacía, falta de `%%`, paréntesis sin cerrar; ausencia de la regla `ERROR` (regla por defecto de Lex).

La lógica de la interfaz (fases, botones, filtros, editor de código, ventana del `.l` con pestañas, exportaciones) se comprobó con 64 verificaciones automáticas sobre el `.wasm` real.

## 9. Alcance

Subconjunto de C: identificadores ASCII, enteros sin sufijos, decimales con cifras a ambos lados del punto, notación científica, cadenas con escapes (`\"`, `\\`, `\n`, `\t`, `\r`), comentarios y los operadores de las reglas. **No** se cubren literales de carácter con apóstrofos, sufijos numéricos ni todas las construcciones de C. `#include` y `#define` solo se reconocen como texto. No se comprueba sintaxis ni tipos. Las posiciones cuentan desde 1; una tabulación es un carácter; cada carácter no ASCII es un símbolo (en Flex real contarían sus bytes). La visualización carácter por carácter es didáctica y no reproduce los búferes internos de Flex.

Referencias: [selección de coincidencias](https://westes.github.io/flex/manual/Matching.html) · [patrones](https://westes.github.io/flex/manual/Patterns.html) · [analizador generado](https://westes.github.io/flex/manual/Generated-Scanner.html)
