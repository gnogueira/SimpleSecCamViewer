package br.com.camxp.tv;
public final class ModelTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) {
        check(Model.channels("").length == 0);
        check(Model.channels("1,2").length == 2);
        check(Model.channels("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16").length == 16);
        for (String s : new String[]{"1,1", "17", "0", "1,", "1,,2", "a", "-1", "1,01"}) {
            boolean rejected = false; try { Model.channels(s); } catch (IllegalArgumentException e) { rejected = true; } check(rejected);
        }
        check(Model.ipv4("192.0.2.1") && !Model.ipv4("256.1.1.1") && !Model.ipv4("1.2.3") && !Model.ipv4("1.2.3.4;command"));
        for (int n = 0; n <= 16; n++) for (int mode = 0; mode <= 2; mode++) {
            int[] g = Model.grid(n, mode, 1920, 1080, 24); check(g[0] >= 1 && g[1] >= 1 && g[0] * g[1] >= n);
        }
        check(Model.grid(2, Model.STACK, 1280, 800, 0)[0] == 1);
        check(Model.grid(2, Model.SIDE, 1280, 800, 0)[0] == 2);
        Model.Watchdog w = new Model.Watchdog(0);
        check(w.sample(44999, 1, true, 0, false) == null);
        check(w.sample(45000, 1, true, 0, false) != null);
        w = new Model.Watchdog(0); check(w.sample(1000, 3, true, 25, true) == null);
        check(w.sample(30999, 3, true, 25, true) == null); check(w.sample(31000, 3, true, 25, true) != null);
        w = new Model.Watchdog(0);
        for (int s = 1; s <= 300; s++) check(w.sample(s * 1000L, 3, true, s * 25, true) == null);
        check(w.healthy); check(w.sample(301000, 7, true, 7500, false) != null);
        w = new Model.Watchdog(0); check(w.sample(120000, 3, false, 0, true) == null);
        check(Model.retryDelay(0) == 60000 && Model.retryDelay(1) == 120000 && Model.retryDelay(2) == 240000 && Model.retryDelay(20) == 300000);
        check(!Model.due(62999, 60000, 60000, true)); check(Model.due(63000, 60000, 60000, true));
        System.out.println("OK: canais, limites, layouts, IP, quadros parados, cena estática, espera crescente e conexões espaçadas.");
    }
}
