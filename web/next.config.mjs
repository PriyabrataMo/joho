/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  // The gateway base URL is read at runtime in the browser from
  // NEXT_PUBLIC_GATEWAY_URL (see lib/api.ts). Nothing to proxy here.
};

export default nextConfig;
