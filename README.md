# SimpleSecCamViewer e CamTV

Aplicativos para visualizar câmeras de um NVR pela rede local: **SimpleSecCamViewer** para Windows XP e **CamTV** para Android TV. Ambos permitem escolher até 16 canais, alternar o layout, usar tela cheia e tentar reconectar transmissões interrompidas. O limite real de câmeras depende do NVR, da rede e do dispositivo.

Os aplicativos usam o substream RTSP no formato específico de NVRs Xiongmai/XMeye, como alguns modelos Giga Security. Não são visualizadores universais para qualquer endereço RTSP ou qualquer NVR. Não gravam vídeo. A compatibilidade com outros aparelhos precisa ser verificada.

## SimpleSecCamViewer — Windows XP

Requer Windows XP SP3 de 32 bits e VLC 3 de 32 bits instalado. O aplicativo carrega a LibVLC da instalação do VLC. Para configurar, copie `windows/iniciar.bat.example` para `iniciar.bat` na pasta do executável e edite IP, usuário, senha e caminho do VLC. O arquivo é lido como texto; não precisa ser executado. Proteja esse arquivo: a senha fica em texto simples. Não o publique.

A lista de canais e o layout são salvos em `camxp.ini`. F11 alterna a tela inteira e Esc retorna à janela. Veja as instruções de compilação em [windows/README.md](windows/README.md).

## CamTV — Android TV

Requer Android 8 ou superior, processador ARM de 32 ou 64 bits e conexão local ao NVR. Configure IP, porta, usuário e senha no primeiro uso. A senha é armazenada de forma criptografada com Android Keystore. O aplicativo incorpora LibVLC e dispensa a instalação separada do VLC. Use as setas e OK do controle; em tela cheia, Voltar mostra a barra.

Veja como compilar em [android/README.md](android/README.md). A assinatura de atualizações deve usar a mesma chave do aplicativo já instalado; nunca publique a chave nem suas senhas.

## Capacidade e segurança

Cada câmera usa uma sessão no NVR. Outros aplicativos compartilham o mesmo limite de conexões. As conexões são espaçadas e, em falhas repetidas, as tentativas aguardam períodos progressivos. A detecção observa o avanço dos quadros e pode não identificar uma imagem congelada que continue sendo retransmitida como quadros novos.

RTSP e credenciais via URL não oferecem confidencialidade por si só: use uma rede confiável e não exponha o NVR diretamente à internet. Configurações locais, senhas, logs, gravações e chaves de assinatura não fazem parte do projeto.

## Licenças

O código próprio deste repositório é oferecido sob [MIT](LICENSE). Componentes de terceiros mantêm suas próprias licenças. O CamTV usa LibVLC da VideoLAN, que possui licença própria; antes de distribuir um APK, confira os avisos e obrigações da versão e dos módulos efetivamente incluídos, inclusive acesso ao código-fonte correspondente e possibilidade de substituição da biblioteca quando aplicável. Consulte [THIRD_PARTY.md](THIRD_PARTY.md).
