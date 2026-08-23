# c-language

## ブラウザに Hello World を表示する

`main.c` は Windows の Winsock（WSA）を使う小さな HTTP サーバーです。
ローカルのブラウザからアクセスすると `Hello World` を表示します。

### C ファイルの役割

- `main.c`: HTTP 通信
- `router.c`: URL と処理の振り分け
- `search.c`: 検索ボタンの処理

新しい処理を追加するときは、`main.c` ではなく `router.c` にルートを追加します。
ビルド時は `*.c` により、フォルダー内の C ファイルが自動的に対象になります。

### ビルド

MSYS2 の **UCRT64 ターミナル**で、リポジトリのフォルダーから実行します。
ターミナルの左側に `UCRT64` と表示されていることを確認してください。
`MSYS` と表示されているターミナルでは `gcc: command not found` になるため、
スタートメニューから `MSYS2 UCRT64` を起動してください。

```bash
make
```

`Makefile` がフォルダー内の `*.c` ファイルを自動でまとめてビルドします。
C ファイルを追加しても、ビルドコマンドの変更は不要です。

現在の `MSYS` ターミナルをそのまま使う場合は、GCC の絶対パスを指定します。

```bash
/c/msys64/ucrt64/bin/gcc.exe main.c -o main.exe -lws2_32
```

または、VS Code の「タスクの実行」から `C: すべての C ファイルをビルド` を選びます。

`make` がインストールされていない環境では、次のコマンドでも同じように
フォルダー内の C ファイルをまとめてビルドできます。

```bash
gcc *.c -o main.exe -lws2_32
```

### 起動

MSYS2 の Bash では、ビルドと起動を別々の行で実行します。

```bash
./main.exe
```

PowerShell では次のように起動します。

```powershell
.\main.exe
```

PowerShell からビルドする場合は、GCC の場所を指定できます。

```powershell
& "C:\msys64\ucrt64\bin\gcc.exe" main.c search.c -o main.exe -lws2_32
```

表示されたらブラウザで次の URL を開きます。

<http://localhost:8080/>

サーバーを終了するには、起動したターミナルで `Ctrl+C` を押します。