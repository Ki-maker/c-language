# c-language

## ブラウザに Hello World を表示する

`main.c` は Windows の Winsock（WSA）を使う小さな HTTP サーバーです。
ローカルのブラウザからアクセスすると `Hello World` を表示します。

### ビルド

MSYS2 の **UCRT64 ターミナル**で、リポジトリのフォルダーから実行します。
ターミナルの左側に `UCRT64` と表示されていることを確認してください。
`MSYS` と表示されているターミナルでは `gcc: command not found` になるため、
スタートメニューから `MSYS2 UCRT64` を起動してください。

```bash
gcc main.c -o main.exe -lws2_32
```

現在の `MSYS` ターミナルをそのまま使う場合は、GCC の絶対パスを指定します。

```bash
/c/msys64/ucrt64/bin/gcc.exe main.c -o main.exe -lws2_32
```

または、VS Code の「タスクの実行」から `C: main.c をビルド` を選びます。

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
& "C:\msys64\ucrt64\bin\gcc.exe" main.c -o main.exe -lws2_32
```

表示されたらブラウザで次の URL を開きます。

<http://localhost:8080/>

サーバーを終了するには、起動したターミナルで `Ctrl+C` を押します。