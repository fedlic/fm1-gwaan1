# GWAAN-1 — アンビエント・ドローン CFW for M-VAVE FM-1

電源を入れるだけで流れ続ける、生成アンビエント専用のカスタムファームウェア。
「ミヒョーン」とうねる FM 音と、「グワーン」と膨らむドローンのための機械です。
弾かなくても音楽が続き、ツマミを回して部屋の空気を変える——そういう楽器を目指しました。

An original generative ambient drone firmware for the M-VAVE FM-1 mini FM synth.
It plays itself from power-on: slow gliding FM voices, deep swelling drones,
distant radio-like beacons, all steered by feel-good knob macros. Never harsh,
by design. (English summary at the bottom.)

## 音の層（4層）

1. **MIHYON（ミヒョーン）** — FM ボイス。モジュレーターのピッチ比が数秒〜十数秒の周期で滑り、倍音がうねります。音は現在の和音フィールドのコードトーンから選ばれます。
2. **GUWAAN（グワーン）** — 低いサブドローン＋パッド。和音フィールド（sus/add9 系）は 35〜50 秒ごとに約10秒かけて静かにクロスフェードします。v0.2 でパッドが3音デチューン・スタックになり、内蔵スピーカーでも太さが伝わる「ボディ発振器」（ルート×4／×3）を追加しました。
3. **AIR（空気）** — バンドパスノイズ（風・水の気配）と、ごく稀に落ちる水滴ベル。
4. **SIGNAL（発信機）** — v0.2 で追加。遠くの送信機／ソナーのような正弦波ピングが、モールスめいた不規則な間隔（1〜4発＋8〜25秒の沈黙）で、深い残響の中に漂います。KNOB4 の上位で量が増えます。

## シーン（PRESETS で切替）

DEEP（深海）／ FOREST（森）／ RAIN（雨）／ STARS（星空）／ DAWN（朝）。
和音フィールドのパレット、層のバランス、画面の色調が変わります。

## 操作

### ツマミ（全部「回して気持ちいい」マクロです）

| 操作 | 働き |
|---|---|
| SELECT | メインフィルター。左でこもる／中央で素通り／右で薄くなる |
| KNOB1 GLIDE | うねり。グライドの速さと深さ。右ほどミヒョーンが強い |
| KNOB2 DEPTH | 深さ。サブの重さ＋スウェルの周期。v0.2 では太さ（ボディ層）も育つ |
| KNOB3 SPACE | 空間。エコーとリバーブの広がり |
| KNOB4 DRIFT | ゆらぎ。ドリフトとシマー。上位では SIGNAL（発信機ピング）の量も増える |

### ボタン・鍵盤

| 操作 | 働き |
|---|---|
| PLAY | 生成の開始／停止 |
| PRESETS | シーン切替 |
| ALGO | 和音フィールドを手動で次へ進める |
| REC 長押し | 今の和音をホールド（ラッチ式。もう一度長押しで解除） |
| SAVE | 現在の設定を保存（保存しないと電源 OFF で戻ります） |
| OCT- / OCT+ | 鍵盤のオクターブ |
| 鍵盤 | シーンのスケールにロックされた音が鳴り、離鍵後 約6秒かけて溶けて消えます |

鍵盤 LED は全体がゆっくり呼吸するようにほんのり光り、押した鍵と SIGNAL のピングに対応する鍵が明るくなります（FM-1 の LED は単色・明るさのみ。色は画面側が担当）。

## インストール

ビルド済みファームウェアは [Releases](../../releases) から `gwaan1.fwsc` をダウンロードし、
ブラウザ・インストーラ **fm-static** で書き込みます。

1. Chrome で https://seajaysec.github.io/fm-static/ を開く
2. `gwaan1.fwsc` を選ぶ
3. FM-1 を USB 接続し、デバイスを選ぶ
4. 「I've backed up what I need」にチェックして書き込み開始（書き込み中は抜かない）
5. 完了したら電源を入れ直す

> **注意（低音について）**：73 Hz のサブは FM-1 の内蔵スピーカーではほぼ聞こえません。
> ヘッドホン／ライン出しが本領です。v0.2 のボディ層でスピーカーでも太さは感じられますが、
> フルレンジで聴くと印象がかなり変わります。

## ビルド方法

GWAAN-1 は単体ではビルドできず、プラットフォーム **fm1-base** のアプリとしてビルドします。

```sh
git clone https://github.com/CharlesVestal/fm1-base.git
# このリポジトリの中身を fm1-base/gwaan1/ に配置する
cd fm1-base
JIELI_TOOLCHAIN=$HOME/.jieli/toolchain python3 tools/build.py gwaan1 --no-ble
# → gwaan1/build/gwaan1.fwsc（identity FM-1_1400010000）
```

JieLi ツールチェーンと AC79 SDK の取得は fm1-base 側の `tools/get_toolchain.sh` /
`tools/setup_toolchain.sh` に従ってください。ホストシミュレータによるテストは
`tests/` にあります（`tools/build.py gwaan1 --host` 系。詳細は BUILD-REPORT.md）。

## ドキュメント

- [SPEC.md](SPEC.md) — 仕様 v0.1（＋v0.2 の方向メモ）
- [BUILD-REPORT.md](BUILD-REPORT.md) — 実装レポート（v0.1／v0.2 の設計・計測・テスト結果）

## ライセンスとクレジット

- **GWAAN-1 のアプリコード**（`app.c`、`app.h`、`tests/` など本リポジトリの著作物）は
  FEDLIC TOKYO（上田哲也）の著作物で、[MIT License](LICENSE) で公開します。
- **プラットフォーム fm1-base** は Charles Vestal によるもので、**MPL-2.0** です。
  本リポジトリには fm1-base 本体は含まれません。fm1-base 由来のファイルや、
  その改変にあたる部分については MPL-2.0 が適用されます。
- GWAAN-1 is built on [fm1-base](https://github.com/CharlesVestal/fm1-base)
  (Charles Vestal, MPL-2.0) and felucca-bsp. Thanks to the FM-1 custom-firmware community.

## English summary

GWAAN-1 turns the M-VAVE FM-1 into a generative ambient drone machine.
Four layers — a gliding FM voice ("mihyon"), a sub drone with slowly
crossfading chord fields ("guwaan"), filtered-noise air with rare water-drop
bells, and (since v0.2) distant beacon pings like a far-off transmitter —
play themselves from power-on across five scenes (DEEP / FOREST / RAIN /
STARS / DAWN). Knobs are musical macros: filter, glide, depth, space, drift.
Download `gwaan1.fwsc` from Releases and flash it with the browser installer
at https://seajaysec.github.io/fm-static/ . Note: the 73 Hz sub is nearly
inaudible on the built-in speaker — use headphones or line out. App code:
MIT (see LICENSE). Platform (fm1-base, not included here): MPL-2.0.

---

© 2026 FEDLIC TOKYO (Tetsuya Ueda). Personal project; not affiliated with M-VAVE.
