import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Dokumentation für das Clean Image von Jeff-Amdg.
export default defineConfig({
  site: 'https://github.com',
  base: '/smalltv-mod',
  integrations: [
    starlight({
      title: 'SmallTV Clean Image',
      description:
        'Schlanke ESP8266-Firmware für GeekMagic SmallTV und SmallTV Ultra mit .stheme-Themes, persistenter Theme-Playlist, WLAN und Recovery-Modus.',
      logo: {
        src: './src/assets/logo.svg',
        replacesTitle: false,
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/Jeff-Amdg/smalltv-mod',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/Jeff-Amdg/smalltv-mod/edit/clean-image/docs/',
      },
      sidebar: [
        { label: 'Übersicht', link: '/' },
        {
          label: 'Erste Schritte',
          items: [
            { label: 'Gerät einrichten', link: '/getting-started/setup/' },
            { label: 'Firmware installieren', link: '/getting-started/flashing/' },
          ],
        },
        {
          label: 'Bedienung',
          items: [
            { label: 'Schnellstart', link: '/manual/quick-start/' },
            { label: 'Einstellungen', link: '/manual/settings/' },
            { label: 'Fehlerbehebung', link: '/manual/troubleshooting/' },
          ],
        },
        {
          label: 'Themes',
          items: [
            { label: 'Themes und Playlist', link: '/features/themes/' },
          ],
        },
        {
          label: 'Entwicklung',
          items: [
            { label: 'Image bauen', link: '/reference/building/' },
            { label: 'Recovery-Theme', link: '/reference/recovery/' },
          ],
        },
      ],
    }),
  ],
});
