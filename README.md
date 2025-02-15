# HCAデコーダ

This program is strictly for decoding to WAV quickly. If you need a HCA streaming library, consider using [libcgss](https://github.com/hozuki/libcgss).

![Screenshot](https://i.imgur.com/5zfgAch.png)

Comparison between single and multi threaded modes of FastHCADecoder and the [hca decoder in C](https://github.com/Ishotihadus/hca). Tests were performed on Ubuntu 18.04 with an i7-2700K. Your mileage may vary with different hardware/OS configurations and compilers.

Sample file can be found here: https://kinomyu.github.io/bgm.hca

# Changes to original
 - Multithreaded CRI HCA decoding (much higher performance)
 - Fixed WAV data chunk size
 - Fixed WAV smpl chunk loop points
 - Fixed looping
 - Added support for newer hca files that require a subkey

# HCAファイルのデコード方法

  HCAファイルをhca.exeにドラッグ＆ドロップすると、同じファイル名のWAVEファイルができます。
  複数ファイルのデコードにも対応してます。
  デコードオプションはデフォルト値のままです。

  デコードオプションを指定したいときは
  オプション指定デコード.batにドラッグ＆ドロップしてください。
  こちらも複数ファイルのデコードに対応してます。


# HCAファイルの復号化方法

  HCAファイルを復号化.batにドラッグ＆ドロップすると、HCAファイル自体が復号化されます。
  上書きされるので注意してください。
  複数ファイルの復号化にも対応してます。


# 仕様

  デフォルトのデコードオプションは
    音量 = 1(倍)
    ビットモード = 16(ビット)
    ループ回数 = 0(回)
    復号鍵 = 0002B875BC731A85 ※ミリシタで使われている鍵
  です。

  HCAファイルにループ情報が入っていた場合、WAVEファイルにsmplチャンクを追加してます。
  ただし、デコードオプションのループ回数が1回以上のときは、smplチャンクを追加せず、直接波形データとして出力します。
  このとき出力される波形データは以下のようになります。
  ※HCAファイルにループ情報が入っていない場合、ループ開始位置とループ終了位置をそれぞれ先頭位置と末尾位置として扱います。
  [先頭位置～ループ終了位置]＋[ループ開始位置～ループ終了位置]×(ループ回数－１)＋[ループ開始位置～末尾位置]

  HCAファイルにコメント情報が入っていた場合、WAVEファイルにnoteチャンクを追加してます。


# 注意事項

  一応バージョンチェックを外してますが
  今後、v2.1以降のHCAが出てきたとき、デコードに失敗する可能性があります。

  HCAヘッダの破損チェックも無効にしています。
  これはヘッダを改変しやすくするためです。
  もし本当に破損していてもエラーになりません。

  暗号テーブルで使用する鍵はゲーム別に異なります。※開発会社によっては同じ鍵を使うことをがあります。
  暗号テーブルの種類が0x38のとき、鍵が異なるとうまくデコードされません。

  復号鍵を指定してデコードするときは
  オプション指定デコード.batをテキストエディタで開いて、デフォルト値設定の復号鍵を変更しておくと楽です。

  CBRのみ対応。VBRはデコードに失敗します。※VBRは存在しない可能性あり。

  コマンドプロンプトの仕様で、&を含むファイルパス(ファイル名やフォルダ名)は
  オプション指定デコード.batや、復号化.batなどのバッチファイルにドラッグ＆ドロップすると
  ファイルが開けず、エラーが出ます。


# 免責事項

  このアプリケーションを利用した事によるいかなる損害も作者は一切の責任を負いません。
  自己の責任の上で使用して下さい。


# その他

  HCAv2.0からヘッダのVBRチェックをやってない痕跡があるので
  最初からCBRのみしか存在しないのかもしれない。

  ATHテーブルもType0しか存在しなかった痕跡あり。

  普通にデコードすると16ビットPCMになるので音質が劣化するよ！
  オプション指定デコードで、ビットモードをfloatにすると劣化しないよ！
  でもHCA自体が非可逆圧縮なので元々劣化してるよ！
  どっちだよ！

