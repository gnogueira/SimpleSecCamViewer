# CamTV

Abra esta pasta no Android Studio com JDK 17 e Android SDK 36, ou compile com:

```sh
./gradlew :app:assembleDebug
```

O APK debug resultante fica em `app/build/outputs/apk/debug/`, salvo quando se define `CAMTV_BUILD_DIR`. Um release sem assinatura pode ser gerado com `./gradlew :app:assembleRelease`. Para atualizar uma instalação existente, assine com a mesma chave usada na instalação anterior; não compartilhe essa chave.

A biblioteca `org.videolan.android:libvlc-all:3.7.7` é obtida dos repositórios configurados no Gradle. O pacote suporta ARMv7 e ARM64, Android 8.0 ou superior. A capacidade de decodificação varia entre TVs.

Teste das regras independentes de Android:

```sh
mkdir -p /tmp/camtv-tests
javac -encoding UTF-8 -d /tmp/camtv-tests app/src/main/java/br/com/camxp/tv/Model.java tests/ModelTest.java
java -cp /tmp/camtv-tests br.com.camxp.tv.ModelTest
```

Os testes de modelo não substituem testar reprodução, layouts, reconexão e navegação no aparelho real.
