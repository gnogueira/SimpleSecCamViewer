package br.com.camxp.tv;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import org.videolan.libvlc.LibVLC;
import org.videolan.libvlc.Media;
import org.videolan.libvlc.MediaPlayer;
import org.videolan.libvlc.interfaces.IMedia;
import org.videolan.libvlc.interfaces.IVLCVout;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService cleanup = Executors.newCachedThreadPool();
    private final Slot[] slots = new Slot[Model.MAX];
    private final ArrayDeque<String> history = new ArrayDeque<>();
    private SharedPreferences prefs;
    private LibVLC vlc;
    private LinearLayout root, toolbar;
    private Grid grid;
    private TextView empty;
    private Button firstButton;
    private String host, user, password;
    private int port, mode;
    private boolean configured, software, running, destroyed, controls = true, connectedOnce;
    private long lastConnection;

    private final class Slot {
        int channel, vout;
        int attempts;
        boolean active, stopping, waiting, statsWarned;
        long due, delayAfterStop, safeReconnectAt;
        Tile tile;
        MediaPlayer player;
        Media media;
        Model.Watchdog watch;
        String state = "Aguardando";
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void log(String value) {
        String stamp = new SimpleDateFormat("HH:mm:ss", Locale.US).format(new Date());
        history.addLast(stamp + " " + value);
        while (history.size() > 80) history.removeFirst();
    }
    private void toast(String text) { Toast.makeText(this, text, Toast.LENGTH_LONG).show(); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences("camtv", MODE_PRIVATE);
        host = prefs.getString("host", "");
        port = prefs.getInt("port", 554); user = prefs.getString("user", "admin");
        mode = prefs.getInt("layout", Model.SIDE); software = prefs.getBoolean("software", false);
        configured = prefs.getBoolean("configured", false);
        try { password = SecretStore.read(prefs); }
        catch (Exception e) { password = ""; configured = false; }
        int[] channels;
        try { channels = Model.channels(prefs.getString("channels", "1,2")); }
        catch (IllegalArgumentException e) { channels = new int[]{1, 2}; log("Lista inválida; usando canais 1 e 2."); }
        for (int i = 0; i < slots.length; i++) {
            Slot s = slots[i] = new Slot();
            if (i < channels.length) { s.channel = channels[i]; s.active = true; }
        }
        root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setBackgroundColor(0xff09141f);
        toolbar = new LinearLayout(this); toolbar.setGravity(Gravity.CENTER_VERTICAL); toolbar.setPadding(dp(8), dp(2), dp(8), dp(2));
        addButton("Câmeras", this::cameraMenu);
        addButton("Layout", this::layoutMenu);
        addButton("NVR", this::configure);
        addButton("Reconectar", () -> reconnectAll("Reconexão manual"));
        addButton("Tela cheia", () -> setControls(false));
        addButton("Mais", this::mainMenu);
        root.addView(toolbar, new LinearLayout.LayoutParams(-1, dp(58)));
        grid = new Grid();
        for (Slot s : slots) { s.tile = new Tile(s); grid.addView(s.tile); }
        empty = new TextView(this); empty.setTextColor(Color.WHITE); empty.setTextSize(20); empty.setGravity(Gravity.CENTER);
        empty.setText("Nenhuma câmera selecionada\nPressione OK/Menu e escolha Câmeras → Adicionar.");
        empty.setFocusable(true); empty.setOnClickListener(v -> cameraMenu()); grid.addView(empty);
        root.addView(grid, new LinearLayout.LayoutParams(-1, 0, 1)); setContentView(root);
        immersive(); refresh(); firstButton.requestFocus();
        try {
            vlc = new LibVLC(getApplicationContext(), new ArrayList<>(Arrays.asList("--no-audio", "--no-osd", "--no-video-title-show", "--stats", "--quiet")));
            log("CamTV 1.1 · LibVLC " + LibVLC.version());
        } catch (Throwable e) {
            android.util.Log.e("CamTV", "Falha na inicialização do mecanismo de vídeo", e);
            log("Não foi possível carregar o mecanismo de vídeo.");
            new AlertDialog.Builder(this).setTitle("CamTV").setMessage("Não foi possível iniciar o mecanismo de vídeo neste aparelho.")
                .setPositiveButton("Fechar", (d, w) -> finish()).show(); return;
        }
        if (!configured) main.postDelayed(this::configure, 300);
    }
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }
    @Override public void onWindowFocusChanged(boolean hasFocus) { super.onWindowFocusChanged(hasFocus); if (hasFocus) { immersive(); refresh(); } }
    private void addButton(String title, Runnable action) {
        Button b = new Button(this); b.setText(title); b.setTextSize(14); b.setAllCaps(false);
        b.setBackgroundResource(br.com.camxp.tv.R.drawable.control_background); b.setTextColor(Color.WHITE);
        b.setOnClickListener(v -> action.run()); b.setMinWidth(0); b.setPadding(dp(5), 0, dp(5), 0);
        toolbar.addView(b, new LinearLayout.LayoutParams(0, -1, 1));
        if (firstButton == null) firstButton = b;
    }
    private int count() { int n = 0; for (Slot s : slots) if (s.active) n++; return n; }
    private boolean selected(int ch, Slot except) {
        for (Slot s : slots) if (s != except && (s.active || s.stopping || s.player != null) && s.channel == ch) return true;
        return false;
    }
    private boolean saveChannels() {
        StringBuilder b = new StringBuilder();
        for (Slot s : slots) if (s.active) { if (b.length() > 0) b.append(','); b.append(s.channel); }
        return prefs.edit().putString("channels", b.toString()).commit();
    }
    private void refresh() {
        if (destroyed || grid == null) return;
        if (running && !hasWindowFocus()) return;
        boolean changed = false;
        for (Slot s : slots) {
            int visible = s.active ? View.VISIBLE : View.GONE;
            if (s.tile.getVisibility() != visible) { s.tile.setVisibility(visible); changed = true; }
            if (!s.active) continue;
            String status = s.state;
            if (s.stopping) status = "Encerrando conexão anterior…";
            else if (s.waiting) status = "Conexão em " + Math.max(0, (s.due - SystemClock.elapsedRealtime() + 999) / 1000) + " s";
            String title = "Câmera " + s.channel + " · " + status;
            if (!title.contentEquals(s.tile.title.getText())) s.tile.title.setText(title);
            int notice = controls || s.stopping || s.waiting || !"Ao vivo".equals(s.state) ? View.VISIBLE : View.GONE;
            if (s.tile.title.getVisibility() != notice) { s.tile.title.setVisibility(notice); changed = true; }
        }
        int emptyVisibility = count() == 0 ? View.VISIBLE : View.GONE;
        if (empty.getVisibility() != emptyVisibility) { empty.setVisibility(emptyVisibility); changed = true; }
        if (changed) grid.requestLayout();
    }
    private void setControls(boolean show) {
        controls = show; toolbar.setVisibility(show ? View.VISIBLE : View.GONE); refresh(); immersive();
        if (show) firstButton.requestFocus();
        else {
            for (Slot s : slots) if (s.active) { s.tile.requestFocus(); break; }
            toast("OK ou Menu abre os controles. Voltar mostra a barra.");
        }
    }
    private void showList(AlertDialog dialog) {
        dialog.show();
        dialog.getListView().setSelector(new android.graphics.drawable.ColorDrawable(0xff007f95));
    }
    private void mainMenu() {
        showList(new AlertDialog.Builder(this).setTitle("CamTV")
                .setItems(new String[]{"Câmeras", "Layout", "Configurar NVR", "Reconectar todas", controls ? "Tela cheia" : "Mostrar barra", "Diagnóstico", "Sair"}, (d, which) -> {
                    switch (which) {
                        case 0: cameraMenu(); break; case 1: layoutMenu(); break; case 2: configure(); break;
                        case 3: reconnectAll("Reconexão manual"); break; case 4: setControls(!controls); break;
                        case 5: diagnostics(); break; case 6: confirmExit(); break;
                    }
                }).create());
    }
    private void cameraMenu() {
        showList(new AlertDialog.Builder(this).setTitle("Câmeras · " + count() + "/16")
                .setItems(new String[]{"Adicionar câmera", "Remover câmera", "Alterar canal", "Reconectar uma câmera"}, (d, which) -> {
                    if (which == 0) chooseChannel(null);
                    else chooseSlot(which == 1 ? "Remover" : which == 2 ? "Alterar canal" : "Reconectar", s -> {
                        if (which == 1) remove(s); else if (which == 2) chooseChannel(s); else retry(s, "Reconexão manual", false);
                    });
                }).create());
    }
    private interface SlotAction { void run(Slot slot); }
    private void chooseSlot(String title, SlotAction action) {
        ArrayList<Slot> values = new ArrayList<>(); ArrayList<String> labels = new ArrayList<>();
        for (Slot s : slots) if (s.active) { values.add(s); labels.add("Câmera " + s.channel); }
        if (values.isEmpty()) { toast("Nenhuma câmera selecionada."); return; }
        showList(new AlertDialog.Builder(this).setTitle(title).setItems(labels.toArray(new String[0]), (d, n) -> action.run(values.get(n))).create());
    }
    private void chooseChannel(Slot current) {
        if (current == null && count() >= Model.MAX) { toast("Limite de 16 câmeras atingido."); return; }
        if (current != null && current.stopping) { toast("Aguarde o encerramento desse painel."); return; }
        ArrayList<Integer> values = new ArrayList<>(); ArrayList<String> labels = new ArrayList<>();
        for (int ch = 1; ch <= Model.MAX; ch++) if (!selected(ch, current)) { values.add(ch); labels.add("Câmera " + ch); }
        if (values.isEmpty()) { toast("Todos os canais estão selecionados ou encerrando."); return; }
        showList(new AlertDialog.Builder(this).setTitle(current == null ? "Adicionar câmera" : "Alterar canal")
                .setItems(labels.toArray(new String[0]), (d, n) -> {
                    int channel = values.get(n); Slot target = current;
                    if (target == null) for (Slot s : slots) if (!s.active && !s.stopping && s.player == null) { target = s; break; }
                    if (target == null) { toast("Aguarde uma sessão anterior encerrar."); return; }
                    if (selected(channel, target)) { toast("Esse canal já está selecionado."); return; }
                    if (target.active && target.channel == channel) return;
                    int previous = target.channel; boolean wasActive = target.active;
                    target.channel = channel; target.active = true;
                    if (!saveChannels()) { target.channel = previous; target.active = wasActive; toast("Não foi possível salvar."); return; }
                    target.attempts = 0;
                    retry(target, wasActive ? "Canal alterado" : "Câmera adicionada", false); refresh();
                }).create());
    }
    private void remove(Slot s) {
        s.active = false;
        if (!saveChannels()) { s.active = true; toast("Não foi possível salvar a remoção."); return; }
        retire(s, 0); log("Câmera " + s.channel + " removida."); refresh(); firstButton.requestFocus();
    }
    private void layoutMenu() {
        showList(new AlertDialog.Builder(this).setTitle("Layout")
                .setSingleChoiceItems(new String[]{"Lado a lado", "Uma acima da outra", "Mosaico automático"}, mode, (d, n) -> {
                    if (prefs.edit().putInt("layout", n).commit()) { mode = n; grid.requestLayout(); }
                    else toast("Não foi possível salvar o layout."); d.dismiss();
                }).create());
    }
    private EditText field(LinearLayout box, String label, String value, int input) {
        TextView text = new TextView(this); text.setText(label); text.setTextColor(Color.WHITE); box.addView(text);
        EditText edit = new EditText(this); edit.setText(value); edit.setSingleLine(true); edit.setInputType(input); box.addView(edit); return edit;
    }
    private void configure() {
        if (destroyed || isFinishing()) return;
        LinearLayout box = new LinearLayout(this); box.setOrientation(LinearLayout.VERTICAL); box.setPadding(dp(24), dp(8), dp(24), dp(8));
        TextView intro = new TextView(this); intro.setText("Conexão direta ao NVR. A senha fica somente nesta TV, criptografada.\nCada câmera usa uma sessão do NVR; confira o limite do seu aparelho.");
        intro.setTextColor(0xffd7e9f2); box.addView(intro);
        EditText ip = field(box, "IP do NVR", host, InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        EditText portField = field(box, "Porta RTSP", String.valueOf(port), InputType.TYPE_CLASS_NUMBER);
        EditText login = field(box, "Usuário", user, InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        EditText secret = field(box, "Senha", password, InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        Button decoder = new Button(this); final boolean[] soft = {software};
        Runnable update = () -> decoder.setText(soft[0] ? "Decodificação: software" : "Decodificação: automática (hardware permitido)"); update.run();
        decoder.setOnClickListener(v -> { soft[0] = !soft[0]; update.run(); }); box.addView(decoder);
        ScrollView scroll = new ScrollView(this); scroll.addView(box);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Configurar NVR")
                .setView(scroll).setNegativeButton("Cancelar", null).setPositiveButton("Salvar e conectar", null).create();
        dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String h = ip.getText().toString().trim(), u = login.getText().toString().trim(); int p;
            try { p = Integer.parseInt(portField.getText().toString()); } catch (NumberFormatException e) { portField.setError("Porta inválida"); return; }
            if (!Model.ipv4(h)) { ip.setError("Informe um endereço IPv4"); return; }
            if (p < 1 || p > 65535) { portField.setError("Porta entre 1 e 65535"); return; }
            if (u.isEmpty()) { login.setError("Informe o usuário"); return; }
            try {
                SharedPreferences.Editor edit = prefs.edit().putString("host", h).putInt("port", p).putString("user", u)
                        .putBoolean("software", soft[0]).putBoolean("configured", true);
                SecretStore.write(edit, secret.getText().toString());
                if (!edit.commit()) throw new IllegalStateException();
            } catch (Exception e) { toast("Não foi possível proteger e salvar a configuração nesta TV."); return; }
            boolean previous = configured;
            host = h; port = p; user = u; password = secret.getText().toString(); software = soft[0]; configured = true;
            for (Slot s : slots) if (s.active) { s.attempts = 0; retire(s, previous ? 60000 : 1000); }
            log("Configuração do NVR salva; senha omitida do diagnóstico."); dialog.dismiss(); refresh();
        }));
        dialog.show();
    }
    private void diagnostics() {
        StringBuilder text = new StringBuilder("CamTV 1.0\nAndroid " + android.os.Build.VERSION.RELEASE + " · "
                + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL + "\nArquiteturas: " + Arrays.toString(android.os.Build.SUPPORTED_ABIS)
                + "\nCâmeras selecionadas: " + count() + "/16\n\n");
        for (String line : history) text.append(line).append('\n');
        TextView body = new TextView(this); body.setText(text); body.setTextIsSelectable(true); body.setPadding(dp(16), dp(8), dp(16), dp(8));
        ScrollView view = new ScrollView(this); view.addView(body);
        new AlertDialog.Builder(this).setTitle("Diagnóstico sem credenciais").setView(view).setPositiveButton("Fechar", null).show();
    }
    private void confirmExit() {
        new AlertDialog.Builder(this).setTitle("Sair do CamTV?").setMessage("As conexões deste aplicativo serão encerradas.")
                .setPositiveButton("Sair", (d, n) -> finish()).setNegativeButton("Continuar", null).show();
    }
    @Override public void onBackPressed() { if (!controls) setControls(true); else confirmExit(); }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getKeyCode() == KeyEvent.KEYCODE_MENU) { mainMenu(); return true; }
        return super.dispatchKeyEvent(event);
    }
    @Override protected void onStart() {
        super.onStart(); running = true; getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        int order = 0;
        if (configured) for (Slot s : slots) if (s.active && !s.stopping && s.player == null && !s.waiting) {
            s.waiting = true; s.due = Math.max(s.safeReconnectAt, now() + 1000 + order++ * 3000L);
        }
        main.post(ticker);
    }
    @Override protected void onStop() {
        running = false; main.removeCallbacks(ticker); getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        for (Slot s : slots) { s.waiting = false; retire(s, 60000); }
        super.onStop();
    }
    @Override protected void onDestroy() { destroyed = true; releaseIfIdle(); super.onDestroy(); }
    private void releaseIfIdle() {
        if (!destroyed) return;
        for (Slot s : slots) if (s.player != null || s.stopping) return;
        if (vlc != null) { vlc.release(); vlc = null; } cleanup.shutdown();
    }
    private long now() { return SystemClock.elapsedRealtime(); }
    private void reconnectAll(String reason) { for (Slot s : slots) if (s.active) { s.attempts = 0; retry(s, reason, false); } }
    private void retry(Slot s, String reason, boolean automatic) {
        if (!s.active) return;
        long delay = automatic ? Model.retryDelay(s.attempts) : 60000;
        if (automatic && s.attempts < 4) s.attempts++;
        log("Câmera " + s.channel + ": " + reason + "; nova tentativa após " + delay / 1000 + " s."); retire(s, delay);
    }
    private void retire(Slot s, long delay) {
        s.delayAfterStop = delay;
        if (s.stopping) return;
        final MediaPlayer old = s.player; final Media media = s.media;
        s.player = null; s.media = null; s.vout = 0; s.waiting = false;
        if (old == null && media == null) { s.waiting = s.active && running && configured; s.due = now() + delay; refresh(); return; }
        s.stopping = true;
        if (old != null) {
            old.setEventListener(null);
            try { if (old.getVLCVout().areViewsAttached()) old.getVLCVout().detachViews(); } catch (RuntimeException ignored) { }
        }
        cleanup.execute(() -> {
            boolean success = false;
            try { if (old != null) { old.stop(); old.release(); } if (media != null) media.release(); success = true; }
            catch (RuntimeException ignored) { }
            final boolean complete = success;
            main.post(() -> {
                if (!complete) { log("Uma sessão não confirmou encerramento; não será sobreposta por outra."); return; }
                s.stopping = false; s.waiting = s.active && running && configured;
                s.safeReconnectAt = now() + 60000; s.due = now() + s.delayAfterStop;
                s.state = "Aguardando"; refresh(); releaseIfIdle();
            });
        }); refresh();
    }
    private void connect(Slot s) {
        if (vlc == null || !running || !configured || !s.active || s.stopping || s.player != null) return;
        s.waiting = false; s.statsWarned = false; s.watch = new Model.Watchdog(now()); s.state = "Conectando";
        try {
            String address = "rtsp://" + host + ":" + port + "/user=" + Uri.encode(user) + "&password=" + Uri.encode(password)
                    + "&channel=" + s.channel + "&stream=1.sdp?real_stream";
            s.media = new Media(vlc, Uri.parse(address)); s.media.setHWDecoderEnabled(!software, false);
            s.media.addOption(":rtsp-tcp"); s.media.addOption(":network-caching=1000"); s.media.addOption(":no-audio");
            MediaPlayer p = s.player = new MediaPlayer(vlc); p.setMedia(s.media); p.setVolume(0);
            IVLCVout out = p.getVLCVout(); out.setVideoView(s.tile.surface);
            out.attachViews((v, width, height, visibleWidth, visibleHeight, sarNum, sarDen) -> main.post(() -> {
                if (s.player != p) return;
                s.tile.videoWidth = width; s.tile.videoHeight = height;
                s.tile.visibleWidth = visibleWidth; s.tile.visibleHeight = visibleHeight;
                s.tile.sarNum = sarNum; s.tile.sarDen = sarDen;
                s.tile.fit();
            }));
            p.setEventListener(event -> {
                if (s.player != p || destroyed) return;
                if (event.type == MediaPlayer.Event.Playing) s.state = "Ao vivo";
                else if (event.type == MediaPlayer.Event.Opening) s.state = "Conectando";
                else if (event.type == MediaPlayer.Event.Vout) s.vout = event.getVoutCount();
                else if (event.type == MediaPlayer.Event.EncounteredError || event.type == MediaPlayer.Event.EndReached) { retry(s, "Conexão encerrada ou com erro", true); return; }
                refresh();
            });
            s.tile.fit(); p.play(); log("Conexão solicitada: câmera " + s.channel + ".");
        } catch (RuntimeException e) { retry(s, "Falha ao iniciar o vídeo", true); }
    }
    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            if (!running || destroyed) return;
            long time = now();
            if (configured && vlc != null) {
                for (Slot s : slots) if (s.active && s.waiting && !s.stopping && Model.due(time, s.due, lastConnection, connectedOnce)
                        && s.tile.surface.getHolder().getSurface().isValid()) {
                    lastConnection = time; connectedOnce = true; connect(s); break;
                }
                for (Slot s : slots) if (s.active && s.player != null && s.watch != null) {
                    try {
                        IMedia.Stats stats = s.media.getStats();
                        if (stats == null && !s.statsWarned) { s.statsWarned = true; log("Contadores indisponíveis na câmera " + s.channel + "; monitorando estado."); }
                        String reason = s.watch.sample(time, s.player.getPlayerState(), stats != null, stats == null ? 0 : stats.displayedPictures, s.vout > 0);
                        if (s.watch.healthy) s.attempts = 0;
                        if (reason != null) retry(s, reason, true);
                    } catch (RuntimeException e) { retry(s, "Falha ao consultar o vídeo", true); }
                }
            }
            refresh(); main.postDelayed(this, 1000);
        }
    };
    private final class Grid extends ViewGroup {
        int columns = 1, rows = 1;
        Grid() { super(MainActivity.this); }
        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            int w = MeasureSpec.getSize(widthSpec), h = MeasureSpec.getSize(heightSpec);
            int[] size = Model.grid(count(), mode, w, h, controls ? dp(24) : 0); columns = size[0]; rows = size[1];
            setMeasuredDimension(w, h); int n = 0;
            for (Slot s : slots) if (s.active) {
                int col = n % columns, row = n++ / columns;
                s.tile.measure(MeasureSpec.makeMeasureSpec((col + 1) * w / columns - col * w / columns, MeasureSpec.EXACTLY),
                        MeasureSpec.makeMeasureSpec((row + 1) * h / rows - row * h / rows, MeasureSpec.EXACTLY));
            }
            empty.measure(MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(h, MeasureSpec.EXACTLY));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            int w = r - l, h = b - t, n = 0;
            for (Slot s : slots) if (s.active) {
                int col = n % columns, row = n++ / columns;
                s.tile.layout(col * w / columns, row * h / rows, (col + 1) * w / columns, (row + 1) * h / rows);
            }
            empty.layout(0, 0, w, h);
        }
    }
    private final class Tile extends LinearLayout {
        final Slot slot; final TextView title; final FrameLayout video; final SurfaceView surface;
        int videoWidth, videoHeight, visibleWidth, visibleHeight, sarNum, sarDen;
        Tile(Slot s) {
            super(MainActivity.this); slot = s; setOrientation(VERTICAL); setFocusable(true); setPadding(dp(2), dp(2), dp(2), dp(2));
            title = new TextView(MainActivity.this); title.setTextColor(Color.WHITE); title.setTextSize(12); title.setSingleLine(true);
            title.setPadding(dp(4), 0, dp(4), 0); addView(title, new LinearLayout.LayoutParams(-1, dp(24)));
            video = new FrameLayout(MainActivity.this); video.setBackgroundColor(Color.BLACK);
            surface = new SurfaceView(MainActivity.this); surface.setFocusable(false); surface.setClickable(false);
            video.addView(surface, new FrameLayout.LayoutParams(-1, -1, Gravity.CENTER));
            addView(video, new LinearLayout.LayoutParams(-1, 0, 1));
            video.addOnLayoutChangeListener((v, a, b, c, d, e, f, g, h) -> fit());
            setOnFocusChangeListener((v, focus) -> {
                GradientDrawable bg = new GradientDrawable(); bg.setColor(Color.BLACK); bg.setStroke(dp(2), focus ? 0xff20bad6 : 0xff142536); setBackground(bg);
            });
            setOnClickListener(v -> { if (controls) showList(new AlertDialog.Builder(MainActivity.this).setTitle("Câmera " + slot.channel)
                    .setItems(new String[]{"Alterar canal", "Remover", "Reconectar", "Menu principal"}, (d, n) -> {
                        if (n == 0) chooseChannel(slot); else if (n == 1) remove(slot); else if (n == 2) retry(slot, "Reconexão manual", false); else mainMenu();
                    }).create()); else mainMenu(); });
            setOnKeyListener((v, key, event) -> {
                if (event.getAction() != KeyEvent.ACTION_DOWN) return false;
                ArrayList<Slot> active = new ArrayList<>(); for (Slot item : slots) if (item.active) active.add(item);
                int index = active.indexOf(slot), next = index;
                if (key == KeyEvent.KEYCODE_DPAD_UP) { if (index < grid.columns) { setControls(true); return true; } next -= grid.columns; }
                else if (key == KeyEvent.KEYCODE_DPAD_DOWN) next += grid.columns;
                else if (key == KeyEvent.KEYCODE_DPAD_LEFT) next--;
                else if (key == KeyEvent.KEYCODE_DPAD_RIGHT) next++;
                else return false;
                if (next >= 0 && next < active.size()) active.get(next).tile.requestFocus(); return true;
            });
        }
        void fit() {
            int w = video.getWidth(), h = video.getHeight();
            MediaPlayer player = slot.player;
            if (w < 1 || h < 1 || player == null || !player.getVLCVout().areViewsAttached()) return;
            player.getVLCVout().setWindowSize(w, h);
            player.setScale(0);
            player.setAspectRatio(null);
            int surfaceWidth = ViewGroup.LayoutParams.MATCH_PARENT;
            int surfaceHeight = ViewGroup.LayoutParams.MATCH_PARENT;
            // Hardware output needs its SurfaceView resized too; OpenGL instead
            // fits the video within the full surface using the player viewport.
            if (videoWidth > 0 && videoHeight > 0 && visibleWidth > 0 && visibleHeight > 0) {
                double aspect = (double) visibleWidth / visibleHeight;
                if (sarNum > 0 && sarDen > 0) aspect *= (double) sarNum / sarDen;
                double displayWidth = w, displayHeight = h;
                if (displayWidth / displayHeight < aspect) displayHeight = displayWidth / aspect;
                else displayWidth = displayHeight * aspect;
                surfaceWidth = Math.max(1, (int) Math.ceil(displayWidth * videoWidth / visibleWidth));
                surfaceHeight = Math.max(1, (int) Math.ceil(displayHeight * videoHeight / visibleHeight));
            }
            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) surface.getLayoutParams();
            if (params.width != surfaceWidth || params.height != surfaceHeight) {
                params.width = surfaceWidth; params.height = surfaceHeight; params.gravity = Gravity.CENTER;
                surface.setLayoutParams(params);
            }
        }
    }
}
