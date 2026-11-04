package llc;

import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.URI;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;

/** Standalone app_process transport; stdout is reserved for the native protocol. */
public final class HttpsFetch {
    static Proxy proxy() throws Exception {
        for (String key : new String[]{"HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy"}) {
            String value = System.getenv(key);
            if (value == null || value.isEmpty()) continue;
            URI uri = new URI(value);
            if (!"http".equalsIgnoreCase(uri.getScheme()) || uri.getHost() == null ||
                    uri.getUserInfo() != null || uri.getQuery() != null || uri.getFragment() != null ||
                    !(uri.getPath().isEmpty() || "/".equals(uri.getPath())))
                throw new IllegalArgumentException("unsupported_proxy");
            int port = uri.getPort() == -1 ? 80 : uri.getPort();
            return new Proxy(Proxy.Type.HTTP, new InetSocketAddress(uri.getHost(), port));
        }
        return Proxy.NO_PROXY;
    }

    static int timeout(long deadline) throws java.net.SocketTimeoutException {
        long remaining = (deadline - System.nanoTime()) / 1000000;
        if (remaining <= 0) throw new java.net.SocketTimeoutException();
        return (int)Math.min(Integer.MAX_VALUE, remaining);
    }

    static int tlsExit(Exception failure) {
        for (Throwable cause = failure; cause != null; cause = cause.getCause()) {
            if (cause instanceof java.security.cert.CertificateException ||
                    cause instanceof java.security.cert.CertPathValidatorException ||
                    cause instanceof javax.net.ssl.SSLPeerUnverifiedException) return 60;
        }
        return 35;
    }

    public static void main(String[] args) {
        try {
            if (args.length != 4) throw new IllegalArgumentException();
            URL url = new URL(args[0]);
            long limit = Long.parseLong(args[1]);
            int seconds = Integer.parseInt(args[2]);
            if (limit <= 0 || limit > 64L * 1024 * 1024 || seconds <= 0 || seconds > 120)
                throw new IllegalArgumentException();
            boolean redirects = "1".equals(args[3]);
            long deadline = System.nanoTime() + seconds * 1000000000L;
            Proxy proxy = proxy();
            for (int hop = 0; ; hop++) {
                if (hop > 0 && !"https".equalsIgnoreCase(url.getProtocol())) System.exit(47);
                if (!"https".equalsIgnoreCase(url.getProtocol()) || url.getUserInfo() != null)
                    throw new IllegalArgumentException("https_required");
                HttpsURLConnection connection = (HttpsURLConnection)url.openConnection(proxy);
                try {
                    connection.setInstanceFollowRedirects(false);
                    connection.setConnectTimeout(Math.min(5000, timeout(deadline)));
                    connection.setReadTimeout(timeout(deadline));
                    connection.setRequestProperty("Accept-Encoding", "identity");
                    connection.setRequestProperty("User-Agent", "LLC-Zygisk-Localizer");
                    int status = connection.getResponseCode();
                    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                        if (!redirects || hop >= 5) System.exit(47);
                        String location = connection.getHeaderField("Location");
                        if (location == null) System.exit(47);
                        url = new URL(url, location);
                        continue;
                    }
                    if (status != 200) System.exit(22);
                    if (connection.getContentLengthLong() > limit) System.exit(63);
                    long received = 0;
                    try (InputStream input = connection.getInputStream()) {
                        byte[] buffer = new byte[32768];
                        while (true) {
                            connection.setReadTimeout(timeout(deadline));
                            int count = input.read(buffer);
                            if (count == -1) break;
                            if (count > limit - received) System.exit(63);
                            System.out.write(buffer, 0, count);
                            received += count;
                        }
                    }
                    System.out.print("\nLLC_HTTP_STATUS:200");
                    System.out.flush();
                    return;
                } finally { connection.disconnect(); }
            }
        } catch (java.net.SocketTimeoutException e) { System.exit(28);
        } catch (javax.net.ssl.SSLException e) { System.exit(tlsExit(e));
        } catch (java.net.UnknownHostException e) { System.exit(6);
        } catch (java.net.ConnectException e) { System.exit(7);
        } catch (IllegalArgumentException e) { System.exit(3);
        } catch (Exception e) { System.exit(1); }
    }
}
