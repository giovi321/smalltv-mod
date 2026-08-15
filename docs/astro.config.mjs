import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Deployed to GitHub Pages at https://giovi321.github.io/smalltv-mod/
export default defineConfig({
  site: 'https://giovi321.github.io',
  base: '/smalltv-mod',
  integrations: [
    starlight({
      title: 'smalltv-mod',
      description:
        'Open-source firmware for the GeekMagic SmallTV and its ESP32-C2 and classic-ESP32 lookalikes: ticker, Claude usage meter, plane radar, and a WireGuard tunnel.',
      logo: {
        src: './src/assets/logo.svg',
        replacesTitle: false,
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/giovi321/smalltv-mod',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/giovi321/smalltv-mod/edit/main/docs/',
      },
      sidebar: [
        { label: 'Home', link: '/' },
        {
          label: 'Getting started',
          items: [
            { label: 'Hardware and variants', link: '/getting-started/hardware/' },
            { label: 'Flashing', link: '/getting-started/flashing/' },
            { label: 'First-time setup', link: '/getting-started/setup/' },
          ],
        },
        {
          label: 'User manual',
          items: [
            { label: 'Quick start', link: '/manual/quick-start/' },
            { label: 'Everyday use', link: '/manual/everyday/' },
            { label: 'Settings explained', link: '/manual/settings/' },
            { label: 'Troubleshooting', link: '/manual/troubleshooting/' },
          ],
        },
        {
          label: 'Features',
          items: [
            { label: 'Stock and crypto ticker', link: '/features/ticker/' },
            { label: 'Claude usage meter', link: '/features/usage/' },
            { label: 'Plane radar', link: '/features/radar/' },
            { label: 'WireGuard VPN', link: '/features/wireguard/' },
            { label: 'Notifications', link: '/features/notify/' },
          ],
        },
        {
          label: 'Reference',
          items: [
            { label: 'Data sources', link: '/reference/data-sources/' },
            { label: 'Building from source', link: '/reference/building/' },
            { label: 'Recovery and credits', link: '/reference/recovery/' },
          ],
        },
      ],
    }),
  ],
});
