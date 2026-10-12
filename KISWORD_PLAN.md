# kisword — plano da versão inicial

Planejamento de 2026-10-09.

**Status (2026-10-09): fase 1 implementada.** Diferenças em relação ao planejado:

- Ligar/desligar Strong, notas, vermelho e parágrafos **re-renderiza** o capítulo a
  partir dos textos já em memória (sem tocar no banco), em vez de mudar propriedades
  das tags. É igualmente rápido e evita os problemas de `invisible` no GTK2.
- Os módulos de teste são gerados em C (`tests/test_module.c`), não com o `sqlite3`
  CLI, que nem sempre está instalado. As amostras reais em `interno/` são verificadas
  por `tests/check-samples.sh`.
- Medido: ~190 ms até a janela aparecer (meta era < 100 ms). Uma janela GTK2 mínima
  com um `GtkTextView` leva ~100 ms no mesmo ambiente; a diferença é quase toda
  GTK/fontconfig. RSS ~34 MB (contra ~27 MB da janela mínima).
- Digitar só um número em "Ir para" vai para aquele capítulo do livro atual.
- A seleção PRIMARY (colar com o botão do meio) ainda leva os números Strong como
  aparecem na tela; só o Ctrl+C gera o texto limpo com referência.

Leitor desktop de Bíblias no formato do MySword (app Android), em C + GTK2, com a mesma
meta dos programas do KiDesktop: abrir rápido, pouca RAM, sem frameworks pesados.

- Programa **separado** do KiDesktop: não entra no `kidesktop-core` nem no
  `kidesktop-base`. Candidato a um futuro `kidesktop-extras`.
- Escopo inicial: **só Bíblias** (`*.bbl.mybible`). Comentários, dicionários e livros
  ficam para depois (seção 9).
- Dependências: `gtk+-2.0` e `sqlite3`. Nada além disso (GLib vem com o GTK).

Motivação: não há leitor desktop conhecido que abra `.mybible` direto; o que existe são
conversores (ex.: BibleMultiConverter) para outros formatos.

---

## 1. O formato, conferido nas amostras

Amostras em `interno/` (fora do repositório, ver `.gitignore`):

| Arquivo | Conteúdo | Observações |
|---|---|---|
| `kjv.bbl.mybible` | KJV inglês, 66 livros, 31102 versos | Strong (`WH`/`WG`), palavras de Jesus, notas, itálico |
| `RVP-2009.bbl.mybible` | português, 66 livros, 31102 versos | texto puro; schema antigo de `Details` |
| `HiSB.bbl.mybible` | hebraico, AT (livros 1–39), 23145 versos | **criptografado** (`encryption=2`, `compressed=1`) |

Cada módulo é um banco **SQLite 3**.

### 1.1 Tabela `Bible`

```sql
CREATE TABLE Bible (Book INT, Chapter INT, Verse INT, Scripture TEXT);
CREATE [UNIQUE] INDEX ... ON Bible (Book, Chapter, Verse);
```

- `Book` 1–66 na ordem protestante (1 = Gênesis, 40 = Mateus, 66 = Apocalipse).
- O nome do índice varia (`bible_key`, `BookChapterVerseIndex`); só importa que exista.
- Nos módulos não criptografados `Scripture` é `TEXT` UTF-8; no HiSB é `BLOB`.
- Nenhuma amostra tem `Verse = 0`, nem `Scripture` nulo ou vazio, mas o código deve
  tolerar os dois.

### 1.2 Tabela `Details`

O **schema varia entre módulos** — as colunas não são fixas:

- KJV: `Title, Description, Abbreviation, Comments, Version, VersionDate, PublishDate,
  Publisher, Author, Creator, Source, EditorialComments, Language, RightToLeft, OT, NT,
  Strong, VerseRules`
- HiSB: igual, sem `EditorialComments`, com `encryption INT, compressed INT`
- RVP: só `Description, Abbreviation, Comments, Version, Font, RightToLeft, OT, NT,
  Apocrypha, Strong` (sem `Title`, sem `Language`)

Portanto: ler com `SELECT * FROM Details LIMIT 1` e mapear **pelo nome da coluna**
(`sqlite3_column_name`), sem assumir nenhuma. Fallbacks: título = `Title` →
`Description` → `Abbreviation` → nome do arquivo; idioma ausente = desconhecido.

`Comments` e `EditorialComments` contêm HTML (mostrados na janela "Sobre o módulo",
com HTML simplificado, ver 3.3).

### 1.3 Tags no texto

Contagem real na KJV:

| Tag | Ocorrências | Significado | Renderização |
|---|---|---|---|
| `<WH####>` | 229274 | Strong hebraico, **depois** da palavra | sobrescrito pequeno `H430`, clicável |
| `<WG####>` | 122399 | Strong grego | idem `G2316` |
| `<FR>…<Fr>` | 30391 | palavras de Jesus | vermelho (opcional) |
| `<FI>…<Fi>` | 21526 | palavras acrescentadas | itálico |
| `<RF>…<Rf>` | 7716 | nota de rodapé (conteúdo entre as tags) | marcador clicável, conteúdo oculto |
| `<CM>` | 4257 | fim de parágrafo | quebra de parágrafo (modo parágrafo) |
| `<FO>…<Fo>` | 3804 | citação do AT no NT | cor/estilo próprio (opcional) |
| `<i>…</i>` | 10 | HTML dentro de notas | itálico |

Também no formato (sem ocorrência nas amostras, mas suportar desde já, é barato):
`<FU>…<Fu>` sublinhado, `<TS>…<Ts>` título de seção, `<CL>` quebra de linha,
`<PI#>`/`<PF#>` recuo, `<WT…>` morfologia (oculta por padrão), `<RF q=x>` (nota com
marcador próprio).

RVP: 5 ocorrências de `<v>17:59</v>` — versos fundidos no texto (1Sm 17:58 contém o 59).
Renderizar o conteúdo como número de verso pequeno.

Detalhes que a amostra revelou:

- Tags se intercalam com Strong: `<FR>For<Fr><WG1063><FR> God<Fr><WG2316>…` — o
  parser não pode assumir aninhamento bem formado; é uma máquina de estados com flags
  liga/desliga, não uma árvore.
- Uma palavra pode ter vários Strong seguidos: `created<WH1254><WH853>`.
- Strong pode vir dentro de trecho com estilo e vice-versa.
- Tag desconhecida: descartada (sem texto), contada em modo debug.

### 1.4 `VerseRules`

Regras de substituição por regex aplicadas ao verso antes de renderizar, uma por linha,
`padrão<TAB>substituição` com `$1`, `$2`. Exemplo (HiSB):

```
<K>(.*?)( )?<k>	<span class='red' dir='rtl'>[</span>$1<span class='red' dir='rtl'>]</span>$2
```

A saída é HTML com `class`. Implementável com `GRegex` (GLib, já é dependência) +
mapeamento de classes conhecidas (`red`, `blue`) para tags de texto. Só o HiSB usa nas
amostras, e ele é criptografado — por isso fica na fase 2.

### 1.5 Módulos criptografados/comprimidos

O HiSB tem `encryption=2` e `compressed=1`, e o `Scripture` é binário. É a proteção de
módulos do próprio MySword; **o kisword não vai contornar essa criptografia**.
Comportamento:

- `Details.encryption` presente e diferente de 0 → módulo listado em cinza, com a
  mensagem "Módulo criptografado do MySword — não suportado". Não abre.
- `compressed != 0` sem criptografia: não há amostra; tratar como não suportado até
  aparecer um módulo assim para descobrir o algoritmo.
- Defesa extra: se `typeof(Scripture) = 'blob'` ou o texto não for UTF-8 válido
  (`g_utf8_validate`), mesma mensagem — nunca mandar bytes inválidos ao Pango.

Para testar RTL/hebraico vamos precisar de outro módulo hebraico **sem** criptografia.

---

## 2. Estrutura do projeto

```
kisword/
  Makefile
  README.md
  KISWORD_PLAN.md
  .gitignore            # interno/, binários
  src/
    main.c              # init GTK, config, janela principal
    window.c/.h         # widgets, menus, atalhos, navegação
    module.c/.h         # descoberta, abertura, Details, consultas
    gbf.c/.h            # parser das tags → spans (sem GTK)
    render.c/.h         # spans → GtkTextBuffer
    books.c/.h          # nomes e abreviações dos livros (pt-BR, en)
    ref.c/.h            # parser de referência ("Jo 3:16", "gn 1", "John 3:16-18")
    config.c/.h         # GKeyFile em ~/.config/kisword/kisword.conf
  tests/
    test_module.c       # gera módulos mínimos (vários schemas) e testa module.c
    test_gbf.c          # parser contra casos conhecidos
    test_ref.c
    check-samples.sh    # --check/--dump contra interno/, se existir
  po/                   # gettext, pt_BR e en
```

`gbf.c` e `ref.c` não dependem de GTK (só GLib), para testar sem interface e
reaproveitar na busca.

Build no estilo dos Makefiles do KiDesktop:

```make
CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
PKGS := gtk+-2.0 sqlite3
```

Alvos `all install uninstall clean test lint` (`lint` = `-Werror` + cppcheck). Dependências
de build: `libgtk2.0-dev libsqlite3-dev` (esta última não está instalada hoje).

---

## 3. Componentes

### 3.1 `module.c` — módulos

```c
typedef struct {
    char    *path;
    char    *title, *abbrev, *language, *font;
    gboolean rtl, ot, nt, strong;
    gboolean supported;        /* FALSE: criptografado/comprimido/inválido */
    char    *unsupported_why;
    char    *verse_rules;      /* NULL se ausente */
    sqlite3 *db;               /* aberto só quando usado */
    sqlite3_stmt *st_chapter;  /* preparado uma vez */
    guint8   nchapters[67];    /* por livro; 0 = livro ausente */
} KsModule;

GPtrArray *ks_module_scan(const char *const *dirs); /* *.bbl.mybible */
gboolean   ks_module_open(KsModule *, GError **);
/* callback por verso, na ordem */
gboolean   ks_module_chapter(KsModule *, int book, int chap,
                             void (*cb)(int verse, const char *text, void *), void *);
```

- Abrir com `sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL)`; nunca escrever no
  módulo.
- Na abertura, uma consulta: `SELECT Book, MAX(Chapter) FROM Bible GROUP BY Book` →
  `nchapters[]` (usa o índice; poucos ms).
- Capítulo: `SELECT Verse, Scripture FROM Bible WHERE Book=?1 AND Chapter=?2 ORDER BY
  Verse`, statement preparado e reaproveitado (`sqlite3_reset`).
- Varredura de diretórios lê só `Details` (abre, lê, fecha) — a lista sai rápida mesmo com
  muitos módulos. Opcional depois: cache por (path, mtime).

Diretórios de módulos, em ordem: `~/.local/share/kisword/modules/`, mais os caminhos de
`modules_dirs` na config. Assim dá para apontar direto para a pasta `mysword/` copiada do
celular.

### 3.2 `gbf.c` — parser

Varre o texto uma vez, emitindo spans:

```c
typedef enum {
    KS_SPAN_TEXT, KS_SPAN_STRONG, KS_SPAN_NOTE, KS_SPAN_PARA,
    KS_SPAN_LINE, KS_SPAN_TITLE, KS_SPAN_VERSENUM, KS_SPAN_MORPH
} KsSpanKind;

enum { KS_ST_ITALIC = 1, KS_ST_RED = 2, KS_ST_OTQUOTE = 4, KS_ST_UNDERLINE = 8,
       KS_ST_BOLD = 16, KS_ST_SUP = 32 };

typedef struct {
    KsSpanKind kind;
    unsigned   style;     /* flags ativas */
    const char *text;     /* fatia do texto original ou de um buffer temporário */
    int        len;
    int        indent;    /* PI# */
} KsSpan;

typedef void (*KsSpanCb)(const KsSpan *, void *);
void ks_gbf_parse(const char *src, KsSpanCb, void *);
char *ks_gbf_plain(const char *src); /* texto sem tags, para busca e cópia */
```

- Máquina de estados: `<`…`>` reconhecido → atualiza flags ou emite span especial; o resto
  é texto. Sem malloc por span (fatias do original).
- `<RF>…<Rf>`: o conteúdo vira **um** span `NOTE` (com HTML simples interno preservado
  para o popup), não texto corrido.
- HTML aceito: `i b u sup sub br p` e entidades `&amp; &lt; &gt; &quot; &apos; &nbsp;
  &#N; &#xN;`. Outras tags HTML: descartadas mantendo o conteúdo.
- `<` que não fecha com `>` → texto literal.

### 3.3 `render.c` — spans → `GtkTextView`

Sem WebKit. Um `GtkTextBuffer` por capítulo, somente leitura, com `GtkTextTag`s criadas uma
vez:

| Tag | Propriedades |
|---|---|
| `verse` | número do verso: negrito, menor, cor discreta |
| `italic`, `underline`, `bold` | o óbvio |
| `red` | `foreground` vermelho (desliga com a opção "palavras de Jesus em vermelho") |
| `otquote` | cor/estilo próprio |
| `strong` | sobrescrito (`rise` + `scale` pequeno), cor de link; `invisible` quando desligado |
| `note` | marcador `*`/`a b c` sobrescrito, cor de link |
| `title` | negrito, maior, linha própria |
| `indent1..N` | `left-margin` |
| `rtl` | aplicado ao widget inteiro (`gtk_widget_set_direction`) quando `RightToLeft=1` |

Cliques sem guardar um objeto por palavra:

- **Strong**: no clique, encontrar o trecho da tag `strong` sob o cursor
  (`gtk_text_iter_backward_to_tag_toggle`/`forward_to_tag_toggle`); o próprio texto
  (`H430`) é o dado. Na fase 1 só mostra o número num popup; a definição vem com
  dicionários (seção 9).
- **Notas**: array `{offset no buffer, texto}` por capítulo; o buffer é reconstruído a
  cada capítulo, então os offsets são estáveis. Clique → popup (`GtkWindow` POPUP com
  `GtkLabel` usando markup Pango gerado do HTML simples).

Modos de exibição: **verso por linha** (padrão) ou **parágrafo** (usa `<CM>`; sem `<CM>`
no módulo, como na RVP, cai para verso por linha). Ao trocar Strong/notas/vermelho só
muda a propriedade da tag, sem reconstruir o buffer.

Marcas por verso (`gtk_text_buffer_create_mark` "v12") para rolar até um verso e
sincronizar painéis paralelos.

### 3.4 `books.c` e `ref.c` — livros e referências

Os módulos não trazem nomes de livros; o kisword traz tabelas próprias (pt-BR e en, nome
completo + abreviações). Idioma da tabela segue o locale da interface.

`ref.c` aceita: `Jo 3:16`, `jo 3.16`, `João 3`, `1Co 13:4-7`, `1 Coríntios 13`, `gn1`,
`John 3:16`. Sem diferença de maiúsculas e acentos (`g_utf8_casefold` + remoção de
diacríticos via `g_utf8_normalize(..., G_NORMALIZE_NFD)`). Prefixo ambíguo → primeiro
livro na ordem canônica. Reutilizável no futuro para referências cruzadas.

### 3.5 `window.c` — interface

```
┌─────────────────────────────────────────────────────────────┐
│ [KJV ▾] [Gênesis ▾] [Cap 1 ▴▾]   [◀][▶]   [Ir para: ____ ]  │
├─────────────────────────────────────────────────────────────┤
│ 1 In the beginning ᴴ⁷²²⁵ God ᴴ⁴³⁰ created ᴴ¹²⁵⁴ ...         │
│ 2 And the earth ᴴ⁷⁷⁶ was ...*                               │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ Gênesis 1 — King James Version of 1611/1769 (KJV)            │
└─────────────────────────────────────────────────────────────┘
```

Atalhos:

| Atalho | Ação |
|---|---|
| `Ctrl+L` | foca "Ir para" |
| `Alt+←` / `Alt+→` | histórico de navegação |
| `Ctrl+PgUp` / `Ctrl+PgDn` | capítulo anterior/próximo |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | tamanho da fonte |
| `Ctrl+C` | copia a seleção com referência ("Jo 3:16 (KJV) — texto"), sem tags |
| `F9` | liga/desliga Strong |
| `Ctrl+F` | busca (fase 2) |

Abrir um módulo não suportado: diálogo com o motivo (`unsupported_why`).

### 3.6 `config.c`

`~/.config/kisword/kisword.conf` (GKeyFile): `modules_dirs`, último módulo e
referência, fonte e tamanho, toggles (strong, notas, vermelho, modo parágrafo), tamanho da
janela. Gravado ao fechar e ao trocar módulo; nunca a cada clique.

---

## 4. Metas de leveza

- Sem WebKit, sem threads na fase 1.
- Abrir na última posição em < 100 ms com o cache de disco quente.
- Trocar de capítulo: 1 consulta indexada + montagem do buffer; o capítulo mais longo da
  KJV (Sl 119, 176 versos, milhares de Strong) deve renderizar sem atraso perceptível.
- RAM: só o capítulo atual em memória; o texto plano para busca só é montado quando a
  busca é usada (fase 2).
- Medir com `/usr/bin/time -v` e registrar no README, como no KiDesktop.

---

## 5. Testes

- `tests/mkfixture.sh` cria um `.bbl.mybible` mínimo com o `sqlite3` CLI: `Details` com
  schema completo, outro com schema estilo RVP, outro com `encryption=2`; e versos com
  todas as tags da seção 1.3, incluindo os casos intercalados reais da KJV.
- `test_gbf`: entrada → sequência de spans esperada; `ks_gbf_plain` sem tags.
- `test_ref`: tabela de strings → (livro, capítulo, verso inicial/final).
- Se `interno/` existir, `make test` roda também uma checagem contra as amostras reais:
  todos os 31102 versos da KJV e da RVP passam pelo parser sem tag desconhecida e com
  UTF-8 válido; o HiSB é recusado como criptografado.

---

## 6. Fase 1 — MVP

1. Makefile, esqueleto da janela, config.
2. `module.c`: varredura, `Details` dinâmico, detecção de não suportados, consultas.
3. `gbf.c` + testes.
4. `render.c`: todas as tags da seção 1.3, modos verso/parágrafo, toggles.
5. `books.c`, `ref.c` + testes; navegação por combo, "Ir para", histórico.
6. Copiar com referência, zoom, última posição.
7. Validar com KJV (Strong, notas, vermelho) e RVP (texto puro, `<v>`).

## 7. Fase 2

- **Busca**: texto plano por verso montado sob demanda (~5 MB por Bíblia), sem
  maiúsculas/acentos, frase exata ou todas as palavras, filtro por intervalo de livros;
  busca por Strong (`H430`) no texto bruto. Resultados num `GtkTreeView`; clique navega.
  **Feito** (`search.c`, `searchpane.c`, Ctrl+F, `--search`):
  - O texto indexado é o que aparece na tela (regras aplicadas, sem títulos, notas nem
    Strong), "dobrado": minúsculas, sem marcas combinantes (acentos, pontos do hebraico,
    marcas de direção), pontuação → um espaço, com espaço nas pontas. Assim "palavras
    inteiras" é só procurar `" amor "`, e frase ignora pontuação. Um bloco de memória
    reduzido ao tamanho exato no fim (~4 MB na ARC).
  - Índice montado em fatias de 300 versos num idle, com barra de progresso: ~150 ms na
    ARC/KJV, ~10 s no BHSE (regras pesadas) sem travar a janela. Descartado ao trocar de
    módulo. Busca no índice: ~10 ms.
  - Strong: consulta `H430`/`g02316` vai direto ao banco (`LIKE '%430%'` filtra, o parser
    confirma), pegando `<WH430>`, `<WH0430>` e os links que as `VerseRules` criam.
  - Modos: todas as palavras, frase exata, qualquer palavra; intervalo: tudo, AT, NT,
    livro atual (o livro do momento da busca).
  - A lista guarda só a posição; o texto de cada linha é lido do banco quando a linha é
    desenhada. Até 10 000 linhas.
  - Percorrer resultados deixa uma só entrada no histórico (o lugar onde a busca começou).
- **Bíblias paralelas**: modo intercalado (um `GtkTextView`, verso a verso de cada
  módulo — alinhamento perfeito, simples) e modo colunas (`GtkHPaned` com rolagem
  sincronizada pelo verso no topo, usando as marcas `vN`).
- `VerseRules` e `span class` (seção 1.4). **Feito** (`rules.c`, `style.c`):
  - Formato real: `padrão⇥substituição[⇥substituição desligada⇥nome]`; as de 4 campos
    são opções que o MySword deixa ligar/desligar — aplicamos a forma "ligada". Sintaxe
    .NET (`$1`, lookbehind, `\1`), lida pelo PCRE do `GRegex` depois de converter
    `$N` → `\g<N>`. Regra que não compila é pulada (o `--check` conta).
  - Regras-identidade (`(ׄ|ׅ|…)⇥$1`, só existem para poderem ser desligadas) são
    descartadas: no ETCBC isso levou a Bíblia toda de 8 s para 0,9 s.
  - Regras aplicadas em `module.c` a todo verso lido; o BHSE (28 regras pesadas) custa
    ~0,3 ms/verso, ~10 ms por capítulo.
  - Classes: cores embutidas (`red`, `blue`, `green`, `brown`, `gray`, …) mais um
    subconjunto do `Details.CustomCSS` (é CSS de WebView, com funções do MySword como
    `@darken(@red,5%)`): `color`, itálico, negrito, sublinhado, `vertical-align`,
    `display:block/none`, para `.c`, `a.c` e elementos próprios (`<gloss>`).
  - `<a class='strong' href='sG3056'>` (o que as regras fazem dos `<WG…>`) volta a ser
    número Strong clicável; `<a class='morph'>`, morfologia.
- "Sobre o módulo" com `Comments`/`EditorialComments`. **Feito** (Ajuda → Sobre o
  módulo): campos do `Details` lidos na hora (`ks_module_details`, funciona também em
  módulo criptografado) e o HTML dos comentários convertido por `html.c` para texto com
  estilos num `GtkTextView`. O HTML real é de página web (tabelas, `<FONT SIZE=7>`,
  `<style>`, quebras de linha no fonte, até 70 KB), por isso um conversor próprio, que
  colapsa espaços como um navegador, em vez do parser de versos. As entidades HTML
  (`&copy;`, `&eacute;`, …) passaram a ser compartilhadas com o parser de versos.

## 8. Empacotamento

`.deb` no mesmo esquema do KiDesktop (`scripts/build-deb.sh`), `.desktop` e ícone. Pacote
próprio `kisword`; se surgir o `kidesktop-extras`, entra como dependência dele.

## 9. Depois das Bíblias (fora do escopo atual)

- `.dct.mybible` (dicionários) → definição real no popup de Strong.
- `.cmt.mybible` (comentários) → painel lateral sincronizado com o verso.
- Backends MyBible (`verses`, livros 10/20/30…) e e-Sword `.bblx`, aproveitando que a
  interface de `module.c` já isola o formato.

## 10. Em aberto

- Módulos com `compressed=1` sem criptografia: algoritmo desconhecido até termos amostra.
- Apócrifos (`Details.Apocrypha`, ausente nas amostras): numeração de livros acima de 66
  a confirmar com um módulo que os tenha.
- Módulo hebraico sem criptografia para testar RTL.
