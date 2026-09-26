import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import Ticked from '@site/src/components/Ticked';
import {
  Platforms,
  StatusLabels,
  Capabilities,
  CapabilityColumns,
} from '@site/src/data/platforms';

function StatusBadge({status}) {
  const {label, className} = StatusLabels[status];
  return <span className={`statusBadge ${className}`}>{label}</span>;
}

function PlatformCard({platform}) {
  const {icon, name, status, languages, distribution, minVersion, docs, api, note} = platform;
  return (
    <div className="col col--4" style={{marginBottom: '1.5rem'}}>
      <div className="platformCard">
        <div className="platformCardHead">
          <span className="platformCardIcon">{icon}</span>
          <span className="platformCardName">{name}</span>
          <StatusBadge status={status} />
        </div>
        <dl className="platformCardMeta">
          <dt>Languages</dt>
          <dd>{languages.join(', ')}</dd>
          <dt>Requires</dt>
          <dd>{minVersion}</dd>
          <dt>Distribution</dt>
          <dd><Ticked>{distribution}</Ticked></dd>
        </dl>
        <p className="platformCardNote"><Ticked>{note}</Ticked></p>
        {(docs || api) && (
          <div className="platformCardLinks">
            {docs && <Link to={docs}>Install →</Link>}
            {api && <Link to={api}>API reference →</Link>}
          </div>
        )}
      </div>
    </div>
  );
}

function Cell({value}) {
  if (value === true) {
    return <td className="matrixCell matrixYes" title="Supported">✓</td>;
  }
  if (value === 'unverified') {
    return <td className="matrixCell matrixUnknown" title="Present in the shared core, never verified on this platform">?</td>;
  }
  return <td className="matrixCell matrixNo" title="Not available">—</td>;
}

export default function PlatformsPage() {
  const byStatus = (status) => Platforms.filter((p) => p.status === status);
  const columns = CapabilityColumns.map((id) => Platforms.find((p) => p.id === id));

  return (
    <Layout
      title="Platforms"
      description="Which platforms Massif Maps supports today, what is coming, and what is inherited but unmaintained.">
      <header className="pageHeader">
        <div className="container">
          <h1>Platforms</h1>
          <p>
            One C++ core, native bindings per platform. Android, iOS and the web are built by CI
            today; the rest is either on the roadmap or inherited from the original CARTO SDK and
            unverified.
          </p>
        </div>
      </header>

      <main className="container margin-vert--xl">
        <h2>Supported today</h2>
        <div className="row">
          {byStatus('supported').map((p) => (
            <PlatformCard key={p.id} platform={p} />
          ))}
        </div>

        <h2 className="margin-top--xl">Coming</h2>
        <p className="sectionLead">
          Planned, tracked on the <Link to="/roadmap">roadmap</Link>. Nothing here is usable yet —
          no artifacts, no API.
        </p>
        <div className="row">
          {byStatus('planned').map((p) => (
            <PlatformCard key={p.id} platform={p} />
          ))}
        </div>

        <h2 className="margin-top--xl">Inherited, unmaintained</h2>
        <p className="sectionLead">
          Still in the tree from the original CARTO SDK. No CI, no artifacts, no one has run them
          against this fork. Contributions to revive them are welcome.
        </p>
        <div className="row">
          {byStatus('legacy').map((p) => (
            <PlatformCard key={p.id} platform={p} />
          ))}
        </div>

        <h2 className="margin-top--xl">Feature matrix</h2>
        <p className="sectionLead">
          <span className="matrixLegend matrixYes">✓</span> supported ·{' '}
          <span className="matrixLegend matrixUnknown">?</span> in the shared core, never verified
          on that platform · <span className="matrixLegend matrixNo">—</span> not available
        </p>
        <div className="matrixWrapper">
          <table className="matrixTable">
            <thead>
              <tr>
                <th>Capability</th>
                {columns.map((p) => (
                  <th key={p.id}>
                    <span className="matrixHeadIcon">{p.icon}</span>
                    <br />
                    {p.name}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {Capabilities.map((cap) => (
                <tr key={cap.name}>
                  <th scope="row">{cap.to ? <Link to={cap.to}>{cap.name}</Link> : cap.name}</th>
                  {CapabilityColumns.map((id) => (
                    <Cell key={id} value={cap.values[id]} />
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <p className="margin-top--lg">
          Want a platform sooner? <Link to="/sponsors">Sponsoring</Link> is what decides the order,
          and <Link to="/community">contributions</Link> are what makes it happen.
        </p>
      </main>
    </Layout>
  );
}
