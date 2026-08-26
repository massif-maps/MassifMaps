import {Fragment} from 'react';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import {AsOf, Engines, Rows, Marks} from '@site/src/data/compare';

/*
 * How Massif Maps sits next to the other mobile map renderers. The data — and the rules the
 * cells are written under — live in src/data/compare.js.
 */

function Cell({entry, self}) {
  const mark = Marks[entry?.v] ?? Marks.unknown;
  return (
    <td className={`compareCell ${self ? 'compareCellSelf' : ''}`}>
      <span className={`compareMark ${mark.className}`} title={mark.label}>
        {mark.glyph}
      </span>
      {entry?.note && <span className="compareNote">{entry.note}</span>}
    </td>
  );
}

export default function ComparePage() {
  return (
    <Layout
      title="Compare"
      description="Massif Maps next to MapLibre GL Native, the Mapbox Maps SDK and Tangram ES.">
      <header className="pageHeader">
        <div className="container">
          <h1>How it compares</h1>
          <p>
            Massif Maps beside the other mobile map renderers. Checked {AsOf}; a cell nobody has
            verified says so rather than guessing.
          </p>
        </div>
      </header>

      <main className="container margin-vert--lg">
        <p className="sectionLead">
          These engines overlap far more than they differ — all four draw vector tiles well. The
          differences worth choosing on are the licence, what ships <em>in</em> the SDK (routing,
          geocoding, contour generation, shadows) and how much of the map you can change at
          runtime.
        </p>

        <div className="matrixWrapper">
          <table className="compareTable">
            <thead>
              <tr>
                <th />
                {Engines.map((engine) => (
                  <th key={engine.id} className={engine.self ? 'compareHeadSelf' : undefined}>
                    <a href={engine.href}>{engine.name}</a>
                    <span className="compareHeadTag">{engine.tag}</span>
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {Rows.map((section) => (
                <Fragment key={section.group}>
                  <tr className="compareGroupRow">
                    <th colSpan={Engines.length + 1}>{section.group}</th>
                  </tr>
                  {section.items.map((row) => (
                    <tr key={row.label}>
                      <th scope="row" className="compareRowLabel">
                        {row.label}
                      </th>
                      {Engines.map((engine) => (
                        <Cell key={engine.id} entry={row[engine.id]} self={engine.self} />
                      ))}
                    </tr>
                  ))}
                </Fragment>
              ))}
            </tbody>
          </table>
        </div>

        <p className="compareLegend">
          <span className="compareMark compareYes">●</span> yes{'  '}
          <span className="compareMark comparePartial">◐</span> partial, see the note{'  '}
          <span className="compareMark compareNo">○</span> no{'  '}
          <span className="compareMark compareUnknown">?</span> not checked
        </p>

        <h2>Where Massif Maps is the weaker choice</h2>
        <ul>
          <li>
            <strong>Only Android and iOS today.</strong> If you need the same renderer on the web,
            MapLibre is one project across native and GL JS. Desktop and web are on{' '}
            <Link to="/roadmap">the roadmap</Link>, not shipped.
          </li>
          <li>
            <strong>CartoCSS, not GL style JSON.</strong> The ecosystem of ready-made styles is
            written for the Mapbox format. The <Link to="/docs/tools/style-cli">style CLI</Link>{' '}
            converts one and reports exactly what it could not carry, but that is a conversion, not
            a drop-in.
          </li>
          <li>
            <strong>Smaller community.</strong> Fewer answered questions, fewer third-party
            bindings, one maintainer team.
          </li>
        </ul>

        <h2>Where it is the stronger one</h2>
        <ul>
          <li>
            <strong>Outdoor and mountain maps.</strong> On-the-fly contours from elevation tiles,
            terrain shadows, a physical sky and the peak-finder relief look are in the SDK rather
            than in your app.
          </li>
          <li>
            <strong>Routing and geocoding without a service.</strong> Valhalla is embedded and runs
            offline; geocoding reads OSM packages on the device.
          </li>
          <li>
            <strong>One API from every language.</strong> The{' '}
            <Link to="/docs/api/">surface API</Link> is ids and JSON, so the same map code reads
            the same in Kotlin, Swift, TypeScript and C — and a new SDK feature reaches every
            binding without a binding change.
          </li>
          <li>
            <strong>No token, no billing, no account.</strong> BSD 3-Clause, your own tiles.
          </li>
        </ul>

        <p className="roadmapFootnote">
          Something here wrong or out of date?{' '}
          <a href="https://github.com/massif-maps/MassifMaps/edit/master/website/src/data/compare.js">
            Correct it in one file
          </a>
          .
        </p>
      </main>
    </Layout>
  );
}
