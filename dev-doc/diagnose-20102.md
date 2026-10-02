# Diagnosticando o #20102 - filmstrip de captura congelado

Status: instrumentação aplicada. Target: `master`.
Arquivo: `src/views/tethering.c`.

---

## 1. O sintoma

Issue #20102: várias imagens são capturadas em tethering e aparecem no
filmstrip. Nenhuma pode ser selecionada, nem com mouse, nem com teclado. As
imagens estão lá; o modelo de seleção não responde.

## 2. Por que isso ainda não tem patch

Duas causas candidatas produzem **exatamente o mesmo sintoma visível**:

**(A) O sinal nunca chega.** O callback de ativação do filmstrip não é
chamado. A UI está populada, mas nada conecta clique/tecla a uma mudança de
seleção.

**(B) O sinal chega com imgid inválido.** O callback roda, mas
`dt_is_valid_imgid(imgid)` é falso, e todo o corpo efetivo é pulado em
silêncio.

São correções completamente diferentes: (A) é provavelmente wiring ou
sinalização; (B) é a origem do sinal. Escolher sem medir é Apostar.

E há um detalhe no código que torna (B) mais provável do que parece:
`lib->image_id` é atribuído **antes** da checagem de validade, então uma
ativação inválida ainda muta o estado que `_capture_view_get_selected_imgid()`
entrega para o resto da aplicação.

## 3. A instrumentação

Dois pontos de log, ambos sob `DT_DEBUG_CAMCTL`.

**Na chegada da imagem** — `_camera_capture_image_downloaded()`, roda no
thread "tethering":

```
[tethering] image downloaded: /path/to/IMG_0041.CR3 thread=7f2a1b3c4d50
```

**Na ativação do filmstrip** — `_view_capture_filmstrip_activate_callback()`:

```
[tethering] filmstrip activate: imgid=143 valid=1 thread=7f2a1b3c4d50
[tethering] filmstrip activate ignored: invalid imgid -1 (this is the #20102 failure mode)
```

Note que a segunda linha "ignored" só aparece quando o imgid é inválido — é
uma mudança real de comportamento, não só log: antes o código caía no `if`
vazio, agora ele sai cedo e registra. As instruções executadas são as mesmas.

## 4. Como rodar

```bash
darktable -d camctl
```

Depois: entre em tethering, dispare 5+ capturas, tente selecionar no
filmstrip com mouse e com setas. Cole a saída do terminal.

## 5. Como ler o resultado

| O que aparece | Diagnóstico | O que fazer |
|---|---|---|
| `image downloaded` × N, `filmstrip activate` × 0 | **(A)** o sinal não chega | investigate a ligação do thumb table com a view de captura; **não** é threading |
| `filmstrip activate` com `valid=0` | **(B)** imgid inválido | a origem do sinal entrega id ruim; corrigir aí |
| ambos aparecem, mesmo thread id, e ainda não seleciona | a seleção acontece mas é perdida depois | aí sim investigar race, com `dev-doc/GUI_Threading.md` |
| `image downloaded` × 0 | o problema é antes: import/download | outro caminho de código |

A comparação de **thread id** entre as duas linhas é o dado que separa
"problema de sinalização" de "problema de concorrência". O download roda no
thread "tethering"; o activate roda em quem o thumb table sinaliza.

## 6. Nota de escopo

Este patch **não** conserta o #20102. Ele converte uma falha silenciosa em uma
falha observável, que é o pré-requisito para escolher entre (A) e (B).

Um patch especulativo de threading aqui seria exatamente o erro descrito em
`dev-doc/GUI_Threading.md` e seria a mesma família dos problemas recentes
(#22455–#22457 no raw denoise, #22055 no mask manager). A instrumentação é o
passo barato; a correção vem com o dado.
