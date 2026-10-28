# SimpleSecCamViewer

## Configuração

1. Instale o VLC 3 de 32 bits compatível com Windows XP.
2. Copie `iniciar.bat.example` para `iniciar.bat`, ao lado de `SimpleSecCamViewer.exe`.
3. Edite o IP do NVR, usuário, senha e caminho do VLC. O IP do exemplo é fictício.
4. Abra `SimpleSecCamViewer.exe`. A configuração de câmeras é feita nos menus do aplicativo.

O arquivo de configuração contém a senha em texto simples. Não compartilhe nem publique esse arquivo. Senhas com caracteres especiais e compatibilidade com modelos de NVR diferentes exigem teste.

## Compilação

Com MinGW-w64 i686 e binutils disponíveis no PATH, execute a partir desta pasta:

```sh
i686-w64-mingw32-windres -I . -i camxp.rc -o camxp-resource.o
i686-w64-mingw32-gcc -std=c99 -Os -march=pentium-m -mwindows -nostdlib -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -Wl,--entry,_WinMainCRTStartup@0 -Wl,--subsystem,windows:5.1 -Wl,--major-os-version,5,--minor-os-version,1 -o SimpleSecCamViewer.exe camxp.c camxp-resource.o -lkernel32 -luser32 -lgdi32 -lgcc
i686-w64-mingw32-strip SimpleSecCamViewer.exe
```

O executável depende de uma instalação compatível do VLC de 32 bits e dos seus plugins. `-march=pentium-m` reflete o dispositivo original; para outros processadores antigos pode ser necessário escolher outro alvo e testar.

## Testes locais

```sh
cc -std=c99 -I. tests/test-layout.c -o /tmp/camxp-test-layout && /tmp/camxp-test-layout
cc -std=c99 -I. tests/test-watchdog.c -o /tmp/camxp-test-watchdog && /tmp/camxp-test-watchdog
```

Os testes exercitam as regras de layout e recuperação, mas não substituem teste no Windows XP com VLC e um NVR.
