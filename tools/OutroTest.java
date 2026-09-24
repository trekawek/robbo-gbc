/* Coffee GB regression for previewing the ending from the pause menu.
 *
 * java --class-path "$COFFEE_GB_CP" tools/OutroTest.java ROM.gbc ROM.noi
 * The linker symbols must be from the same ROM build. Input goes through the
 * production menu; the loaded level, actors, board and rendering stay live.
 */
import eu.rekawek.coffeegb.core.Gameboy;
import eu.rekawek.coffeegb.core.GameboyType;
import eu.rekawek.coffeegb.core.cpu.Cpu;
import eu.rekawek.coffeegb.core.events.EventBus;
import eu.rekawek.coffeegb.core.events.EventBusImpl;
import eu.rekawek.coffeegb.core.gpu.Display;
import eu.rekawek.coffeegb.core.joypad.Button;
import eu.rekawek.coffeegb.core.joypad.ButtonPressEvent;
import eu.rekawek.coffeegb.core.joypad.ButtonReleaseEvent;
import eu.rekawek.coffeegb.core.serial.SerialEndpoint;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;

@SuppressWarnings("deprecation")
public class OutroTest implements AutoCloseable {
    private static final int WIDTH = 16, HEIGHT = 31, CELL_BYTES = 14;
    private static final int KEY = 7, LCDC = 0xff40, WY = 0xff4a;
    private final EventBus bus = new EventBusImpl();
    private final Gameboy gb;
    private final Map<String, Integer> symbols = new HashMap<>();
    private final int board, robbo, pack, score, currentBank;
    private long ticks;
    private int frames;

    private OutroTest(Path rom, Path noi) throws Exception {
        var pattern = Pattern.compile("^DEF\\s+(\\S+)\\s+0x([0-9a-fA-F]+)$");
        for (String line : Files.readAllLines(noi)) {
            var match = pattern.matcher(line);
            if (match.matches()) symbols.put(match.group(1), Integer.parseInt(match.group(2), 16));
        }
        board = symbol("_board");
        robbo = symbol("_robbo");
        pack = symbol("_level_packs");
        score = symbol("_gr_score");
        currentBank = symbol("__current_bank");
        gb = new Gameboy.GameboyConfiguration(rom.toFile())
                .setBootstrapMode(Gameboy.BootstrapMode.SKIP)
                .setGameboyType(GameboyType.CGB).setSupportBatterySave(false).build();
        gb.init(bus, SerialEndpoint.NULL_ENDPOINT, null);
        bus.register((Display.GbcFrameReadyEvent event) -> frames++,
                Display.GbcFrameReadyEvent.class);
    }

    private int symbol(String name) {
        Integer value = symbols.get(name);
        if (value == null) throw new IllegalArgumentException("Missing symbol " + name);
        return value;
    }
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private int b(int address) { return gb.getAddressSpace().getByte(address) & 255; }
    private int word(int address) { return b(address) | b(address + 1) << 8; }
    private long dword(int address) {
        return (long)b(address) | (long)b(address + 1) << 8
                | (long)b(address + 2) << 16 | (long)b(address + 3) << 24;
    }
    private void put(int address, int value) { gb.getAddressSpace().setByte(address, value & 255); }
    private void putWord(int address, int value) { put(address, value); put(address + 1, value >> 8); }
    private int cell(int x, int y) { return board + (x * HEIGHT + y) * CELL_BYTES; }
    private int tile(int x, int y) {
        return gb.getGpu().getVideoRam0().getByte(0x9c00 + y * 32 + x) & 255;
    }
    private boolean at(int address) {
        return gb.getCpu().getState() == Cpu.State.OPCODE
                && gb.getCpu().getRegisters().getPC() == (address & 65535)
                && (address < 0x4000 || b(currentBank) == address >> 16);
    }
    private void until(BooleanSupplier condition, String description) {
        long deadline = ticks + 45_000_000;
        while (!condition.getAsBoolean()) {
            check(ticks < deadline, "timeout waiting for " + description
                    + " at PC=" + Integer.toHexString(gb.getCpu().getRegisters().getPC()));
            gb.tick();
            ticks++;
        }
    }
    private void next(int address) {
        until(() -> !at(address), "leave function entry");
        until(() -> at(address), "function " + Integer.toHexString(address));
    }
    private void runTicks(int count) { gb.runTicks(count); ticks += count; }
    private void runFrames(int count) {
        int end = frames + count;
        until(() -> frames >= end, "display frames");
    }
    private void press(Button button) { bus.post(new ButtonPressEvent(button)); }
    private void release(Button button) { bus.post(new ButtonReleaseEvent(button)); }
    private void tap(Button button) {
        press(button); runFrames(4); release(button); runFrames(4);
    }
    private byte[] boardSnapshot() {
        byte[] data = new byte[WIDTH * HEIGHT * CELL_BYTES];
        for (int i = 0; i < data.length; i++) data[i] = (byte)b(board + i);
        return data;
    }
    private void textAt(int x, int y, String text) {
        for (int i = 0; i < text.length(); i++) {
            int expected = 128 + text.charAt(i) - 32;
            check(tile(x + i, y) == expected,
                    "menu/ending text " + text + " differs at " + (x + i) + "," + y);
        }
    }
    private boolean endingTextReady() {
        String heading = "CONGRATULATIONS", prompt = "PRESS START";
        for (int i = 0; i < heading.length(); i++)
            if ((gb.getGpu().getVideoRam0().getByte(0x9800 + 3 * 32 + 2 + i) & 255) !=
                    128 + heading.charAt(i) - 32) return false;
        for (int i = 0; i < prompt.length(); i++)
            if ((gb.getGpu().getVideoRam0().getByte(0x9800 + 15 * 32 + 4 + i) & 255) !=
                    128 + prompt.charAt(i) - 32) return false;
        return true;
    }

    private void test() {
        runTicks(5_000_000);
        press(Button.START);
        runTicks(1_200_000);
        release(Button.START);
        next(symbol("_update_game"));
        check(word(pack + 4) == 1, "fixture did not load level 1");
        int key = -1;
        for (int x = 0; x < WIDTH; x++) for (int y = 0; y < HEIGHT; y++)
            if (b(cell(x, y)) == KEY) { key = cell(x, y); break; }
        check(key >= 0, "level 1 has no key to verify board preservation");

        // A distinct score and ammo count catch an accidental new game or
        // level_init() call even when the visible playfield looks similar.
        for (int i = 0; i < 4; i++) put(score + i, 123456 >> (8 * i));
        putWord(robbo + 14, 7);
        press(Button.START);
        next(symbol("_pause_gr"));
        release(Button.START);
        runFrames(10);
        check(b(WY) == 0, "pause menu did not cover the game");
        textAt(6, 8, "RESUME");
        textAt(6, 10, "RESTART");
        textAt(6, 12, "WARP");
        textAt(6, 14, "OUTRO");
        textAt(6, 16, "QUIT");
        for (int i = 0; i < 6; i++)
            check(tile(7 + i, 5) == 139 + "123456".charAt(i) - '0',
                    "pause menu did not show the preserved score");
        check(tile(4, 8) == 230, "cursor did not start on RESUME");

        byte[] before = boardSnapshot();
        long beforeScore = dword(score);
        int beforeLevel = word(pack + 4), beforeX = word(robbo);
        int beforeY = word(robbo + 2), beforeAmmo = word(robbo + 14);
        int beforeMode = word(symbol("_game_mode"));
        byte[] beforeKey = Arrays.copyOfRange(before, key - board, key - board + CELL_BYTES);
        for (int i = 0; i < 3; i++) tap(Button.DOWN);
        check(tile(4, 14) == 230, "three DOWN presses did not select OUTRO");
        press(Button.A);
        runFrames(2); // pause_gr waits for A to be released before returning
        release(Button.A);
        next(symbol("_ending_gr_show"));
        until(this::endingTextReady, "ending congratulations screen");
        check(b(WY) == 0 || (b(LCDC) & 0x20) == 0,
                "ending left the pause window covering its scene");
        press(Button.START);
        runFrames(2); // ending_gr_show also waits for the confirming key to go up
        release(Button.START);
        next(symbol("_render_gr_camera_resume"));

        check(word(pack + 4) == beforeLevel, "outro preview changed the selected level");
        check(dword(score) == beforeScore, "outro preview changed the score");
        check(word(robbo) == beforeX && word(robbo + 2) == beforeY,
                "outro preview moved Robbo");
        check(word(robbo + 14) == beforeAmmo, "outro preview changed ammo");
        check(word(symbol("_game_mode")) == beforeMode,
                "outro preview entered the canonical end/game mode");
        check(Arrays.equals(boardSnapshot(), before), "outro preview changed the live board");
        check(Arrays.equals(Arrays.copyOfRange(boardSnapshot(), key - board,
                key - board + CELL_BYTES), beforeKey), "outro preview changed a key cell");
        runFrames(12);
        check(b(WY) == 128 && (b(LCDC) & 0x20) != 0,
                "game HUD was not restored after the outro");
        press(Button.START);
        next(symbol("_pause_gr"));
        release(Button.START);
        runFrames(10);
        textAt(6, 14, "OUTRO");
        check(b(WY) == 0, "game cannot be paused again after the outro");
        for (int i = 0; i < 4; i++) tap(Button.DOWN);
        check(tile(4, 16) == 230, "four DOWN presses did not select QUIT");
        press(Button.A);
        runFrames(2);
        release(Button.A);
        next(symbol("_title_gr_show"));
        System.out.println("PASS pause-menu OUTRO preview, ending, state preservation, second pause and QUIT");
    }

    @Override public void close() { try { gb.close(); } finally { bus.close(); } }
    public static void main(String[] args) throws Exception {
        if (args.length != 2)
            throw new IllegalArgumentException("Usage: OutroTest ROM.gbc ROM.noi");
        try (OutroTest test = new OutroTest(Path.of(args[0]), Path.of(args[1]))) {
            test.test();
        }
    }
}
