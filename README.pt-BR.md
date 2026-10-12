# kisword

**Português** · [English](README.md)

Um leitor de Bíblia leve para o desktop, para módulos do **MySword**
(`*.bbl.mybible`), em C e GTK2.

O MySword é um aplicativo de Bíblia para Android. Seus módulos são bancos
SQLite e, que eu saiba, nada no desktop os lê diretamente — só existem
conversores para outros formatos. O kisword lê os módulos como são: copie os
arquivos do celular e abra.

É irmão do KiDesktop, com o mesmo objetivo — abrir rápido, ficar pequeno —
mas é um programa separado; pode vir a fazer parte de um futuro
`kidesktop-extras`.

## Recursos

- Bíblias no formato MySword, com as marcações que elas usam desenhadas
  nativamente (sem WebKit): palavras de Jesus em vermelho, palavras
  acrescentadas em itálico, citações do AT, títulos de seção, marcas de
  parágrafo, recuos.
- A formatação do próprio módulo: `VerseRules` (as substituições por regex
  que o MySword aplica a cada versículo) e a parte simples do `CustomCSS` —
  cores de classes, itálico, negrito, sobrescrito, elementos ocultos e de
  bloco.
- Exibir → Opções do módulo: as opções que os módulos definem nas suas
  VerseRules (vogais e acentos do hebraico, léxico, glosas, variantes…),
  lembradas por módulo.
- Números de Strong como pequenos sobrescritos clicáveis (F9 os esconde).
- Notas de rodapé como marcadores clicáveis que abrem um popup.
- Um versículo por linha ou em parágrafo.
- "Ir para" com referências livres em português ou inglês: `Jo 3:16`,
  `1Co 13:4-7`, `gn 1`, `João 3`, `John 3:16`, ou só `5` para o capítulo 5.
- Busca (Ctrl+F) num painel lateral: todas as palavras, frase exata ou
  qualquer palavra, sem distinguir maiúsculas, acentos (e pontos hebraicos),
  palavras inteiras opcional, limitada ao Antigo/Novo Testamento ou ao livro
  atual. Um número de Strong (`H430`, `G26`) acha os versículos marcados com
  ele. O índice é montado na primeira busca em cada módulo, em segundo plano.
- Bíblias paralelas (F8): até sete ao lado da principal, em colunas que
  rolam juntas ou intercaladas versículo a versículo numa só visão (módulos
  da direita para a esquerda mantêm a direção linha a linha).
- Copiar (Ctrl+C) dá versículos inteiros em texto simples com a referência:
  `Jo 3:16-17 (KJV)` seguido dos versículos.
- Histórico de voltar/avançar, zoom, escolha de fonte, última posição
  lembrada.
- Ajuda → Sobre o módulo mostra os detalhes do módulo e sua descrição (o HTML
  de `Details.Comments`) formatada.
- Módulos da direita para a esquerda são exibidos nessa direção.
- Interface em inglês e português do Brasil; os nomes dos livros seguem o
  idioma do sistema.

- Painel de estudo à direita (F4) com **comentários** (`*.cmt.mybible`,
  acompanhando o versículo mostrado ou clicado), **dicionários**
  (`*.dct.mybible`: lista de palavras, busca enquanto digita, números de
  Strong — clique num deles na Bíblia e a definição abre), **livros**
  (`*.bok.mybible`) e **diários** (`*.jor.mybible`). O HTML deles é desenhado
  nativamente: listas, cores do CSS do módulo, imagens (clique para ver em
  tamanho real) e os links do MySword — para versículos, outros verbetes,
  outros dicionários e livros, notas em popup. Ctrl+D procura a palavra
  selecionada na Bíblia.
- Pastas de módulos copiadas do celular funcionam como estão: `bibles/`,
  `commentaries/`, `dictionaries/`, `books/` e `journals/` dentro de um
  diretório de módulos também são lidas.

Testado com uma coleção de 125 Bíblias do MySword: todas abrem, e todas,
exceto as duas criptografadas, são lidas sem marcações desconhecidas —
títulos, linhas de poesia, interlineares, referências cruzadas, o HTML
produzido por VerseRules, módulos que misturam testamentos da direita para a
esquerda e da esquerda para a direita.

Interlineares e módulos com anotação linguística são dispostos como no
MySword: cada palavra uma coluna (a palavra sobre seu número de Strong,
morfologia, transliteração e glosa), correndo da direita para a esquerda no
hebraico, e as caixas de anotação aninhadas com suas bordas. Exibir →
Interlinear em colunas desliga isso, voltando ao texto corrido.

**Módulos criptografados não são suportados.** Alguns módulos do MySword (por
exemplo, os com `encryption` definido na tabela `Details`) são protegidos
pelo próprio MySword; o kisword os lista esmaecidos e explica o motivo, e não
tenta contornar essa proteção.

## Instalação

Cada versão tem pacotes `.deb` na página de
[Releases](../../releases), para Ubuntu 22.04, 24.04 e 26.04, Debian 12 e 13,
e Devuan daedalus e excalibur (use o pacote do Debian 12 / 13,
respectivamente). Tags `v1.x.x` em diante são versões estáveis; `v0.x.x` e
tags com sufixo (`v1.2.0-beta`) são marcadas como pré-lançamento.

```sh
sudo apt install ./kisword_0.3.0-1_<distro>_amd64.deb
```

## Compilação

Precisa de `libgtk2.0-dev`, `libsqlite3-dev` e gettext.

```sh
make
make test        # testes unitários; também checa módulos reais em interno/ se existirem
sudo make install
```

`make lint` compila com `-Werror` e roda o cppcheck.

## Uso

```sh
kisword                         # módulos de ~/.local/share/kisword/modules
kisword caminho/para/kjv.bbl.mybible --ref "Jo 3:16"
kisword --dump "Sl 23" rvp.bbl.mybible   # texto simples na saída, sem precisar de display
kisword --check *.bbl.mybible            # lê todos os versículos e relata problemas
kisword --search "amor próximo" arc.bbl.mybible   # versículos com todas as palavras
kisword --search H430 --phrase ...       # --phrase, --whole-words como no painel
```

Coloque os módulos em `~/.local/share/kisword/modules/`, liste outros
diretórios em `modules_dirs=` em `~/.config/kisword/kisword.conf`, ou use
Arquivo → Abrir módulo.

| Atalho | Ação |
|---|---|
| Ctrl+L | Ir para a referência |
| Ctrl+F | Buscar (Esc fecha o painel) |
| F8 | Bíblias paralelas |
| Ctrl+PgUp / Ctrl+PgDn | Capítulo anterior / seguinte |
| Alt+← / Alt+→ | Voltar / avançar |
| Ctrl++ / Ctrl+- / Ctrl+0 | Zoom |
| F9 | Números de Strong ligados/desligados |
| Ctrl+C | Copiar versículos com a referência |
| F4 | Painel de estudo |
| Alt+1 … Alt+4 | Comentário, dicionário, livros, diários |
| Ctrl+D | Procurar a palavra selecionada |
| Ctrl+O | Abrir módulo |

## Leveza, medida

No Xvfb, abrindo a KJV em Gênesis 1 ou no Salmo 119 (o capítulo mais longo):

| | kisword | janela GTK2 mínima com um text view |
|---|---|---|
| Início até a janela aparecer | ~190 ms | ~100 ms |
| RSS máximo | ~34 MB | ~27 MB |
| Binário | 122 KB | — |

Ler os 31.102 versículos da KJV leva ~25 ms (`kisword --check`). A maior
parte da partida é o GTK e o fontconfig; trocar de capítulo é uma única
consulta indexada e renderiza em poucos milissegundos.

## Licença

GPL-3.0, veja [LICENSE](LICENSE).
