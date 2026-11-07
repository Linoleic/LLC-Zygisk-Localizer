import com.sun.net.httpserver.HttpsServer;
import com.sun.net.httpserver.HttpsConfigurator;
import java.net.InetSocketAddress;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyStore;
import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;

public final class HttpsFixture {
    public static void main(String[] args) throws Exception {
        KeyStore store = KeyStore.getInstance("PKCS12");
        try (java.io.InputStream input = Files.newInputStream(Paths.get(args[0]))) {
            store.load(input, "fixture-password".toCharArray());
        }
        KeyManagerFactory keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
        keys.init(store, "fixture-password".toCharArray());
        SSLContext tls = SSLContext.getInstance("TLS");
        tls.init(keys.getKeyManagers(), null, null);
        HttpsServer server = HttpsServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.setHttpsConfigurator(new HttpsConfigurator(tls));
        server.setExecutor(java.util.concurrent.Executors.newCachedThreadPool());
        server.createContext("/", exchange -> {
            try {
                String path = exchange.getRequestURI().getPath();
                if (path.equals("/redirect") || path.equals("/loop") || path.equals("/insecure")) {
                    exchange.getResponseHeaders().set("Location", path.equals("/redirect") ? "/ok" : path.equals("/loop") ? "/loop" : "http://localhost/ok");
                    exchange.sendResponseHeaders(302, -1);
                } else if (path.equals("/forbidden") || path.equals("/limited") || path.equals("/missing")) {
                    exchange.sendResponseHeaders(path.equals("/forbidden") ? 403 : path.equals("/limited") ? 429 : 404, -1);
                } else if (path.equals("/error")) {
                    exchange.sendResponseHeaders(503, -1);
                } else {
                    if (path.equals("/slow")) Thread.sleep(3000);
                    byte[] bytes = "fixture-body".getBytes(java.nio.charset.StandardCharsets.UTF_8);
                    exchange.sendResponseHeaders(200, path.equals("/chunked") ? 0 : bytes.length);
                    exchange.getResponseBody().write(bytes);
                }
            } catch (Exception ignored) { /* Timeout clients may disconnect. */
            } finally { exchange.close(); }
        });
        server.start();
        System.out.println(server.getAddress().getPort());
        System.out.flush();
    }
}
