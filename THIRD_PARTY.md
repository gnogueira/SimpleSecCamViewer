# Componentes de terceiros

- **LibVLC Android 3.7.7**, da VideoLAN: incorporada ao CamTV via `org.videolan.android:libvlc-all`. A LibVLC é disponibilizada sob LGPL; componentes e módulos distribuídos junto com o binário podem ter condições adicionais. Consulte o artefato e os fontes da versão exata antes de publicar um APK: https://code.videolan.org/videolan/libvlcjni e https://repo.maven.apache.org/maven2/org/videolan/android/libvlc-all/3.7.7/.
- **VLC / LibVLC no Windows**, da VideoLAN: o SimpleSecCamViewer usa uma instalação separada, fornecida pelo usuário. https://www.videolan.org/vlc/libvlc.html
- **Gradle Wrapper**, distribuído pelo projeto Gradle, sob Apache License 2.0: https://github.com/gradle/gradle.
- **Android Gradle Plugin e dependências transitivas**: confira os metadados e licenças das versões efetivamente resolvidas ao montar cada release.

A licença MIT do código próprio não substitui as licenças de terceiros. É necessário completar a revisão dos avisos, licenças, fontes correspondentes e direitos de substituição/revinculação antes de redistribuir os binários, especialmente o APK com bibliotecas nativas incluídas.
