# Neam Package Registry - Implementation Specification

## Using Lovable for registry.neam.dev

A comprehensive guide to building the Neam package ecosystem using Lovable (lovable.dev) for rapid full-stack development.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Lovable Project Setup](#lovable-project-setup)
3. [Database Schema](#database-schema)
4. [API Endpoints](#api-endpoints)
5. [Frontend Pages](#frontend-pages)
6. [Authentication Flow](#authentication-flow)
7. [Package Upload/Download](#package-uploaddownload)
8. [Search Implementation](#search-implementation)
9. [Deployment Strategy](#deployment-strategy)
10. [Integration with neam-pkg CLI](#integration-with-neam-pkg-cli)

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         NEAM PACKAGE ECOSYSTEM                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────┐     ┌──────────────────────────────────────────────────┐  │
│  │   neam-pkg   │     │              registry.neam.dev                    │  │
│  │     CLI      │────▶│  ┌────────────────────────────────────────────┐  │  │
│  └──────────────┘     │  │           Lovable Frontend                  │  │  │
│                       │  │  (React + TypeScript + Tailwind + shadcn)   │  │  │
│                       │  └────────────────────────────────────────────┘  │  │
│                       │                       │                           │  │
│                       │                       ▼                           │  │
│                       │  ┌────────────────────────────────────────────┐  │  │
│                       │  │           Supabase Backend                  │  │  │
│                       │  │  ┌──────────┐ ┌──────────┐ ┌────────────┐  │  │  │
│                       │  │  │ Auth     │ │ Database │ │ Storage    │  │  │  │
│                       │  │  │ (JWT)    │ │ (Postgres)│ │ (S3-like)  │  │  │  │
│                       │  │  └──────────┘ └──────────┘ └────────────┘  │  │  │
│                       │  └────────────────────────────────────────────┘  │  │
│                       └──────────────────────────────────────────────────┘  │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        CDN (Cloudflare)                               │   │
│  │              Fast package downloads worldwide                         │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Technology Stack (Lovable Default)

| Layer | Technology | Purpose |
|-------|------------|---------|
| Frontend | React + TypeScript | UI components |
| Styling | Tailwind CSS + shadcn/ui | Modern design system |
| Backend | Supabase | Auth, Database, Storage |
| Database | PostgreSQL | Package metadata |
| Storage | Supabase Storage | Package files (.neampkg) |
| CDN | Cloudflare | Fast global downloads |
| Search | PostgreSQL Full-Text | Package search |
| Hosting | Lovable/Vercel | Frontend hosting |

---

## 2. Lovable Project Setup

### Step 1: Create New Project in Lovable

```
Project Name: neam-registry
Description: Package registry for the Neam programming language
```

### Step 2: Initial Prompt for Lovable

Copy this prompt into Lovable to bootstrap the project:

```
Create a package registry website similar to crates.io or npmjs.com for a
programming language called "Neam".

The registry should include:

1. Homepage with:
   - Hero section explaining Neam packages
   - Search bar for packages
   - Featured/popular packages grid
   - Recent packages list
   - Download statistics

2. Package listing page with:
   - Search and filter functionality
   - Sort by downloads, recent, alphabetical
   - Category/keyword filtering
   - Pagination

3. Package detail page with:
   - Package name, version, description
   - Installation command (neam-pkg install package-name)
   - README rendering (markdown)
   - Version history with changelog
   - Dependencies list
   - Download statistics graph
   - Author information
   - License badge
   - Repository link

4. User authentication:
   - Sign up / Login with email
   - GitHub OAuth integration
   - User profile page
   - API token management

5. Package publishing dashboard:
   - My packages list
   - Upload new package
   - Manage versions
   - View download analytics

6. API documentation page

Use Supabase for backend with:
- User authentication
- PostgreSQL database
- File storage for packages

Design should be modern, clean, dark mode by default with light mode toggle.
Use shadcn/ui components.
```

### Step 3: Supabase Configuration

In Lovable, connect Supabase and configure:

```sql
-- Enable required extensions
CREATE EXTENSION IF NOT EXISTS pg_trgm;  -- For fuzzy search
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
```

---

## 3. Database Schema

### SQL Schema for Supabase

```sql
-- ============================================
-- Users (extends Supabase auth.users)
-- ============================================
CREATE TABLE public.profiles (
    id UUID PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
    username VARCHAR(64) UNIQUE NOT NULL,
    display_name VARCHAR(128),
    email VARCHAR(255) NOT NULL,
    avatar_url TEXT,
    bio TEXT,
    website VARCHAR(512),
    github_username VARCHAR(64),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Enable RLS
ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Public profiles are viewable by everyone"
    ON public.profiles FOR SELECT
    USING (true);

CREATE POLICY "Users can update own profile"
    ON public.profiles FOR UPDATE
    USING (auth.uid() = id);

-- ============================================
-- API Tokens
-- ============================================
CREATE TABLE public.api_tokens (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES public.profiles(id) ON DELETE CASCADE,
    name VARCHAR(64) NOT NULL,
    token_hash VARCHAR(255) NOT NULL,
    token_prefix VARCHAR(8) NOT NULL,  -- First 8 chars for identification
    scopes TEXT[] DEFAULT '{"publish", "read"}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    last_used_at TIMESTAMP WITH TIME ZONE,
    expires_at TIMESTAMP WITH TIME ZONE,
    revoked BOOLEAN DEFAULT FALSE
);

ALTER TABLE public.api_tokens ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Users can view own tokens"
    ON public.api_tokens FOR SELECT
    USING (auth.uid() = user_id);

CREATE POLICY "Users can create own tokens"
    ON public.api_tokens FOR INSERT
    WITH CHECK (auth.uid() = user_id);

CREATE POLICY "Users can revoke own tokens"
    ON public.api_tokens FOR UPDATE
    USING (auth.uid() = user_id);

-- ============================================
-- Packages
-- ============================================
CREATE TABLE public.packages (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name VARCHAR(64) UNIQUE NOT NULL,
    owner_id UUID REFERENCES public.profiles(id) ON DELETE SET NULL,
    description TEXT,
    repository VARCHAR(512),
    documentation VARCHAR(512),
    homepage VARCHAR(512),
    license VARCHAR(64),
    readme TEXT,
    readme_html TEXT,  -- Pre-rendered HTML
    keywords TEXT[] DEFAULT '{}',
    categories TEXT[] DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    downloads BIGINT DEFAULT 0,

    -- Search optimization
    search_vector tsvector GENERATED ALWAYS AS (
        setweight(to_tsvector('english', coalesce(name, '')), 'A') ||
        setweight(to_tsvector('english', coalesce(description, '')), 'B') ||
        setweight(to_tsvector('english', coalesce(array_to_string(keywords, ' '), '')), 'C')
    ) STORED
);

CREATE INDEX idx_packages_search ON public.packages USING GIN(search_vector);
CREATE INDEX idx_packages_name ON public.packages(name);
CREATE INDEX idx_packages_downloads ON public.packages(downloads DESC);
CREATE INDEX idx_packages_created ON public.packages(created_at DESC);
CREATE INDEX idx_packages_keywords ON public.packages USING GIN(keywords);

ALTER TABLE public.packages ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Packages are viewable by everyone"
    ON public.packages FOR SELECT
    USING (true);

CREATE POLICY "Package owners can update"
    ON public.packages FOR UPDATE
    USING (auth.uid() = owner_id);

-- ============================================
-- Package Versions
-- ============================================
CREATE TABLE public.versions (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    package_id UUID REFERENCES public.packages(id) ON DELETE CASCADE,
    version VARCHAR(64) NOT NULL,
    neam_version VARCHAR(32),  -- Minimum Neam version required
    checksum VARCHAR(128) NOT NULL,  -- SHA256
    size_bytes BIGINT,
    download_url TEXT NOT NULL,
    published_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    published_by UUID REFERENCES public.profiles(id),
    yanked BOOLEAN DEFAULT FALSE,
    yank_reason TEXT,
    downloads BIGINT DEFAULT 0,
    changelog TEXT,

    -- Metadata from neam.toml
    metadata JSONB DEFAULT '{}',

    UNIQUE(package_id, version)
);

CREATE INDEX idx_versions_package ON public.versions(package_id);
CREATE INDEX idx_versions_published ON public.versions(published_at DESC);

ALTER TABLE public.versions ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Versions are viewable by everyone"
    ON public.versions FOR SELECT
    USING (true);

-- ============================================
-- Dependencies
-- ============================================
CREATE TABLE public.dependencies (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    version_id UUID REFERENCES public.versions(id) ON DELETE CASCADE,
    dependency_name VARCHAR(64) NOT NULL,
    version_req VARCHAR(64) NOT NULL,
    optional BOOLEAN DEFAULT FALSE,
    dev_only BOOLEAN DEFAULT FALSE,
    features TEXT[] DEFAULT '{}'
);

CREATE INDEX idx_dependencies_version ON public.dependencies(version_id);
CREATE INDEX idx_dependencies_name ON public.dependencies(dependency_name);

ALTER TABLE public.dependencies ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Dependencies are viewable by everyone"
    ON public.dependencies FOR SELECT
    USING (true);

-- ============================================
-- Download Statistics
-- ============================================
CREATE TABLE public.download_stats (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    package_id UUID REFERENCES public.packages(id) ON DELETE CASCADE,
    version_id UUID REFERENCES public.versions(id) ON DELETE CASCADE,
    date DATE NOT NULL,
    downloads INTEGER DEFAULT 0,

    UNIQUE(version_id, date)
);

CREATE INDEX idx_download_stats_package ON public.download_stats(package_id, date);
CREATE INDEX idx_download_stats_date ON public.download_stats(date);

-- ============================================
-- Package Owners (for teams)
-- ============================================
CREATE TABLE public.package_owners (
    package_id UUID REFERENCES public.packages(id) ON DELETE CASCADE,
    user_id UUID REFERENCES public.profiles(id) ON DELETE CASCADE,
    role VARCHAR(32) DEFAULT 'maintainer',  -- 'owner', 'maintainer'
    added_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    added_by UUID REFERENCES public.profiles(id),

    PRIMARY KEY (package_id, user_id)
);

ALTER TABLE public.package_owners ENABLE ROW LEVEL SECURITY;

-- ============================================
-- Audit Log
-- ============================================
CREATE TABLE public.audit_log (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES public.profiles(id),
    action VARCHAR(64) NOT NULL,
    entity_type VARCHAR(32) NOT NULL,
    entity_id UUID,
    details JSONB DEFAULT '{}',
    ip_address INET,
    user_agent TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

CREATE INDEX idx_audit_log_user ON public.audit_log(user_id);
CREATE INDEX idx_audit_log_entity ON public.audit_log(entity_type, entity_id);
CREATE INDEX idx_audit_log_created ON public.audit_log(created_at DESC);

-- ============================================
-- Functions
-- ============================================

-- Function to increment download count
CREATE OR REPLACE FUNCTION increment_downloads(p_version_id UUID)
RETURNS void AS $$
BEGIN
    -- Update version downloads
    UPDATE public.versions
    SET downloads = downloads + 1
    WHERE id = p_version_id;

    -- Update package total downloads
    UPDATE public.packages p
    SET downloads = downloads + 1
    FROM public.versions v
    WHERE v.id = p_version_id AND p.id = v.package_id;

    -- Update daily stats
    INSERT INTO public.download_stats (package_id, version_id, date, downloads)
    SELECT v.package_id, p_version_id, CURRENT_DATE, 1
    FROM public.versions v WHERE v.id = p_version_id
    ON CONFLICT (version_id, date)
    DO UPDATE SET downloads = download_stats.downloads + 1;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Function to search packages
CREATE OR REPLACE FUNCTION search_packages(
    search_query TEXT,
    limit_count INTEGER DEFAULT 20,
    offset_count INTEGER DEFAULT 0
)
RETURNS TABLE (
    id UUID,
    name VARCHAR(64),
    description TEXT,
    latest_version VARCHAR(64),
    downloads BIGINT,
    updated_at TIMESTAMP WITH TIME ZONE,
    rank REAL
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        p.id,
        p.name,
        p.description,
        (SELECT v.version FROM public.versions v
         WHERE v.package_id = p.id AND NOT v.yanked
         ORDER BY v.published_at DESC LIMIT 1) as latest_version,
        p.downloads,
        p.updated_at,
        ts_rank(p.search_vector, websearch_to_tsquery('english', search_query)) as rank
    FROM public.packages p
    WHERE p.search_vector @@ websearch_to_tsquery('english', search_query)
    ORDER BY rank DESC, p.downloads DESC
    LIMIT limit_count
    OFFSET offset_count;
END;
$$ LANGUAGE plpgsql;

-- Trigger to update package updated_at
CREATE OR REPLACE FUNCTION update_package_timestamp()
RETURNS TRIGGER AS $$
BEGIN
    UPDATE public.packages SET updated_at = NOW() WHERE id = NEW.package_id;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trigger_update_package_timestamp
    AFTER INSERT ON public.versions
    FOR EACH ROW
    EXECUTE FUNCTION update_package_timestamp();
```

---

## 4. API Endpoints

### Supabase Edge Functions

Create these edge functions in Lovable/Supabase:

#### `/functions/v1/packages/search`

```typescript
// supabase/functions/packages-search/index.ts
import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const { searchParams } = new URL(req.url)
    const query = searchParams.get('q') || ''
    const limit = parseInt(searchParams.get('limit') || '20')
    const offset = parseInt(searchParams.get('offset') || '0')

    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_ANON_KEY') ?? ''
    )

    const { data, error } = await supabaseClient
      .rpc('search_packages', {
        search_query: query,
        limit_count: limit,
        offset_count: offset
      })

    if (error) throw error

    return new Response(
      JSON.stringify({ packages: data }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' } }
    )
  } catch (error) {
    return new Response(
      JSON.stringify({ error: error.message }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 400 }
    )
  }
})
```

#### `/functions/v1/packages/publish`

```typescript
// supabase/functions/packages-publish/index.ts
import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const authHeader = req.headers.get('Authorization')
    if (!authHeader) {
      throw new Error('Missing authorization header')
    }

    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? '',
      { global: { headers: { Authorization: authHeader } } }
    )

    // Get user from token
    const { data: { user }, error: authError } = await supabaseClient.auth.getUser()
    if (authError || !user) throw new Error('Invalid token')

    // Parse multipart form data
    const formData = await req.formData()
    const packageFile = formData.get('package') as File
    const manifestJson = formData.get('manifest') as string

    if (!packageFile || !manifestJson) {
      throw new Error('Missing package file or manifest')
    }

    const manifest = JSON.parse(manifestJson)
    const { name, version, description, license, repository, keywords } = manifest

    // Validate package name
    if (!/^[a-z][a-z0-9_-]*$/.test(name)) {
      throw new Error('Invalid package name. Use lowercase letters, numbers, underscores, and hyphens.')
    }

    // Check if package exists
    const { data: existingPackage } = await supabaseClient
      .from('packages')
      .select('id, owner_id')
      .eq('name', name)
      .single()

    let packageId: string

    if (existingPackage) {
      // Verify ownership
      if (existingPackage.owner_id !== user.id) {
        // Check if user is a maintainer
        const { data: ownership } = await supabaseClient
          .from('package_owners')
          .select('role')
          .eq('package_id', existingPackage.id)
          .eq('user_id', user.id)
          .single()

        if (!ownership) {
          throw new Error('You do not have permission to publish this package')
        }
      }
      packageId = existingPackage.id

      // Check if version already exists
      const { data: existingVersion } = await supabaseClient
        .from('versions')
        .select('id')
        .eq('package_id', packageId)
        .eq('version', version)
        .single()

      if (existingVersion) {
        throw new Error(`Version ${version} already exists`)
      }
    } else {
      // Create new package
      const { data: newPackage, error: createError } = await supabaseClient
        .from('packages')
        .insert({
          name,
          owner_id: user.id,
          description,
          license,
          repository,
          keywords: keywords || []
        })
        .select()
        .single()

      if (createError) throw createError
      packageId = newPackage.id
    }

    // Calculate checksum
    const arrayBuffer = await packageFile.arrayBuffer()
    const hashBuffer = await crypto.subtle.digest('SHA-256', arrayBuffer)
    const hashArray = Array.from(new Uint8Array(hashBuffer))
    const checksum = hashArray.map(b => b.toString(16).padStart(2, '0')).join('')

    // Upload package file to storage
    const storagePath = `packages/${name}/${version}/${name}-${version}.neampkg`
    const { error: uploadError } = await supabaseClient.storage
      .from('packages')
      .upload(storagePath, packageFile, {
        contentType: 'application/octet-stream',
        upsert: false
      })

    if (uploadError) throw uploadError

    // Get public URL
    const { data: urlData } = supabaseClient.storage
      .from('packages')
      .getPublicUrl(storagePath)

    // Create version record
    const { data: newVersion, error: versionError } = await supabaseClient
      .from('versions')
      .insert({
        package_id: packageId,
        version,
        neam_version: manifest.neam_version || '1.0',
        checksum,
        size_bytes: packageFile.size,
        download_url: urlData.publicUrl,
        published_by: user.id,
        metadata: manifest
      })
      .select()
      .single()

    if (versionError) throw versionError

    // Add dependencies
    if (manifest.dependencies) {
      const deps = Object.entries(manifest.dependencies).map(([name, req]) => ({
        version_id: newVersion.id,
        dependency_name: name,
        version_req: typeof req === 'string' ? req : (req as any).version || '*',
        optional: false,
        dev_only: false
      }))

      if (deps.length > 0) {
        await supabaseClient.from('dependencies').insert(deps)
      }
    }

    // Log audit
    await supabaseClient.from('audit_log').insert({
      user_id: user.id,
      action: 'publish',
      entity_type: 'version',
      entity_id: newVersion.id,
      details: { package_name: name, version }
    })

    return new Response(
      JSON.stringify({
        success: true,
        package: name,
        version,
        url: `https://registry.neam.dev/packages/${name}`
      }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' } }
    )
  } catch (error) {
    return new Response(
      JSON.stringify({ error: error.message }),
      { headers: { ...corsHeaders, 'Content-Type': 'application/json' }, status: 400 }
    )
  }
})
```

#### `/functions/v1/packages/download`

```typescript
// supabase/functions/packages-download/index.ts
import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

serve(async (req) => {
  try {
    const { searchParams } = new URL(req.url)
    const name = searchParams.get('name')
    const version = searchParams.get('version')

    if (!name || !version) {
      throw new Error('Missing name or version')
    }

    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // Get version info
    const { data: versionData, error } = await supabaseClient
      .from('versions')
      .select('id, download_url, checksum')
      .eq('version', version)
      .eq('package_id',
        supabaseClient.from('packages').select('id').eq('name', name)
      )
      .single()

    if (error || !versionData) {
      throw new Error('Package version not found')
    }

    // Increment download count
    await supabaseClient.rpc('increment_downloads', {
      p_version_id: versionData.id
    })

    // Redirect to storage URL
    return Response.redirect(versionData.download_url, 302)
  } catch (error) {
    return new Response(
      JSON.stringify({ error: error.message }),
      { headers: { 'Content-Type': 'application/json' }, status: 404 }
    )
  }
})
```

### REST API Summary

| Endpoint | Method | Auth | Description |
|----------|--------|------|-------------|
| `/api/v1/packages` | GET | No | List all packages |
| `/api/v1/packages/search?q=` | GET | No | Search packages |
| `/api/v1/packages/{name}` | GET | No | Get package info |
| `/api/v1/packages/{name}/{version}` | GET | No | Get version info |
| `/api/v1/packages/popular` | GET | No | Popular packages |
| `/api/v1/packages/recent` | GET | No | Recent packages |
| `/api/v1/download/{name}/{version}` | GET | No | Download package |
| `/api/v1/packages` | POST | Yes | Publish package |
| `/api/v1/packages/{name}/{version}` | DELETE | Yes | Yank version |
| `/api/v1/auth/login` | POST | No | Login |
| `/api/v1/auth/register` | POST | No | Register |
| `/api/v1/auth/token` | POST | Yes | Generate API token |
| `/api/v1/users/{username}` | GET | No | Get user profile |
| `/api/v1/users/{username}/packages` | GET | No | User's packages |

---

## 5. Frontend Pages

### Lovable Component Prompts

Use these prompts in Lovable to generate each page:

#### Homepage

```
Create a homepage for neam package registry with:

1. Dark gradient hero section with:
   - Large "Neam Packages" title
   - Tagline: "The package registry for Neam - AI-first programming"
   - Large search input with icon
   - Quick stats: "X packages, Y downloads"

2. "Getting Started" section with code snippets:
   - Install: neam-pkg install <package>
   - Publish: neam-pkg publish

3. "Popular Packages" grid (4 columns) showing:
   - Package name (link)
   - Short description
   - Download count
   - Latest version badge

4. "Recent Updates" list showing:
   - Package name
   - Version
   - Published time (relative)

5. Footer with links to docs, GitHub, Discord

Use shadcn/ui Card, Badge, Input components.
Dark mode with purple/blue accent colors.
```

#### Package Search Page

```
Create a package search results page with:

1. Search bar at top (same as homepage)

2. Filter sidebar:
   - Categories checkboxes (agents, utilities, rag, tools)
   - Sort dropdown (downloads, recent, alphabetical)

3. Results list showing for each package:
   - Package name (large, clickable)
   - Description (2 lines max)
   - Keywords as badges
   - Download count
   - Latest version
   - Last updated (relative time)
   - Install command with copy button

4. Pagination at bottom

Use shadcn/ui components.
Responsive: sidebar collapses to dropdown on mobile.
```

#### Package Detail Page

```
Create a package detail page with:

1. Header section:
   - Package name (large)
   - Version badge (latest)
   - Install command with copy button: neam-pkg install {name}
   - Action buttons: GitHub, Documentation, Homepage

2. Tab navigation:
   - Readme (default)
   - Versions
   - Dependencies
   - Dependents

3. Readme tab:
   - Rendered markdown content
   - Syntax highlighting for code blocks

4. Sidebar (right):
   - Install command
   - Weekly downloads chart (small)
   - License badge
   - Repository link
   - Documentation link
   - Author with avatar
   - Keywords as clickable badges
   - Version history (last 5)

5. Versions tab:
   - Version list with:
     - Version number
     - Published date
     - Download count
     - Changelog (expandable)
     - Yanked badge if applicable

6. Dependencies tab:
   - Runtime dependencies list
   - Dev dependencies list (collapsible)

Use shadcn/ui Tabs, Badge, Card, Avatar components.
Support dark mode.
```

#### User Dashboard

```
Create a user dashboard page with:

1. Header with user avatar and username

2. Tab navigation:
   - My Packages
   - API Tokens
   - Settings

3. My Packages tab:
   - "Publish New Package" button
   - Table of packages:
     - Name
     - Latest version
     - Downloads
     - Last updated
     - Actions (Edit, Analytics)

4. API Tokens tab:
   - "Create New Token" button
   - Table of tokens:
     - Name
     - Prefix (first 8 chars)
     - Scopes
     - Created date
     - Last used
     - Actions (Revoke)
   - Create token dialog:
     - Token name input
     - Scope checkboxes (read, publish)
     - Expiration dropdown
     - Shows token ONCE after creation

5. Settings tab:
   - Profile form:
     - Display name
     - Bio
     - Website
     - GitHub username
   - Danger zone:
     - Delete account button

Use shadcn/ui Table, Dialog, Form, Button components.
```

#### Publish Page

```
Create a package publish page with:

1. Step indicator (3 steps)

2. Step 1: Package Info
   - Drag-drop zone for .neampkg file
   - OR: Connect GitHub repo dropdown
   - Preview of detected neam.toml

3. Step 2: Review
   - Package name and version
   - Description
   - README preview
   - Dependencies list
   - File size

4. Step 3: Confirm
   - Publishing checklist:
     - [ ] Package name follows guidelines
     - [ ] Version is valid semver
     - [ ] README is included
     - [ ] License is specified
   - "Publish" button

5. Success state:
   - Confetti animation
   - Link to package page
   - Install command with copy

Use shadcn/ui Stepper pattern, Dropzone, Checkbox.
```

---

## 6. Authentication Flow

### Supabase Auth Configuration

In Lovable/Supabase dashboard:

1. **Email Auth**: Enable with email confirmation
2. **GitHub OAuth**: Add GitHub provider
3. **Magic Link**: Enable for passwordless login

### Auth Context (React)

```typescript
// src/contexts/AuthContext.tsx
import { createContext, useContext, useEffect, useState } from 'react'
import { supabase } from '@/lib/supabase'
import type { User, Session } from '@supabase/supabase-js'

interface AuthContextType {
  user: User | null
  session: Session | null
  profile: Profile | null
  loading: boolean
  signIn: (email: string, password: string) => Promise<void>
  signUp: (email: string, password: string, username: string) => Promise<void>
  signInWithGitHub: () => Promise<void>
  signOut: () => Promise<void>
}

interface Profile {
  id: string
  username: string
  display_name: string | null
  avatar_url: string | null
}

const AuthContext = createContext<AuthContextType | undefined>(undefined)

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [user, setUser] = useState<User | null>(null)
  const [session, setSession] = useState<Session | null>(null)
  const [profile, setProfile] = useState<Profile | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    // Get initial session
    supabase.auth.getSession().then(({ data: { session } }) => {
      setSession(session)
      setUser(session?.user ?? null)
      if (session?.user) {
        fetchProfile(session.user.id)
      }
      setLoading(false)
    })

    // Listen for auth changes
    const { data: { subscription } } = supabase.auth.onAuthStateChange(
      async (event, session) => {
        setSession(session)
        setUser(session?.user ?? null)
        if (session?.user) {
          await fetchProfile(session.user.id)
        } else {
          setProfile(null)
        }
      }
    )

    return () => subscription.unsubscribe()
  }, [])

  const fetchProfile = async (userId: string) => {
    const { data } = await supabase
      .from('profiles')
      .select('*')
      .eq('id', userId)
      .single()
    setProfile(data)
  }

  const signIn = async (email: string, password: string) => {
    const { error } = await supabase.auth.signInWithPassword({ email, password })
    if (error) throw error
  }

  const signUp = async (email: string, password: string, username: string) => {
    // Check username availability
    const { data: existing } = await supabase
      .from('profiles')
      .select('id')
      .eq('username', username)
      .single()

    if (existing) {
      throw new Error('Username already taken')
    }

    const { data, error } = await supabase.auth.signUp({
      email,
      password,
      options: {
        data: { username }
      }
    })
    if (error) throw error

    // Create profile
    if (data.user) {
      await supabase.from('profiles').insert({
        id: data.user.id,
        username,
        email
      })
    }
  }

  const signInWithGitHub = async () => {
    const { error } = await supabase.auth.signInWithOAuth({
      provider: 'github',
      options: {
        redirectTo: `${window.location.origin}/auth/callback`
      }
    })
    if (error) throw error
  }

  const signOut = async () => {
    const { error } = await supabase.auth.signOut()
    if (error) throw error
  }

  return (
    <AuthContext.Provider value={{
      user,
      session,
      profile,
      loading,
      signIn,
      signUp,
      signInWithGitHub,
      signOut
    }}>
      {children}
    </AuthContext.Provider>
  )
}

export const useAuth = () => {
  const context = useContext(AuthContext)
  if (!context) {
    throw new Error('useAuth must be used within AuthProvider')
  }
  return context
}
```

---

## 7. Package Upload/Download

### Storage Configuration

In Supabase:

```sql
-- Create storage bucket
INSERT INTO storage.buckets (id, name, public)
VALUES ('packages', 'packages', true);

-- Storage policies
CREATE POLICY "Anyone can download packages"
    ON storage.objects FOR SELECT
    USING (bucket_id = 'packages');

CREATE POLICY "Authenticated users can upload"
    ON storage.objects FOR INSERT
    WITH CHECK (
        bucket_id = 'packages'
        AND auth.role() = 'authenticated'
    );
```

### Package Format (.neampkg)

The `.neampkg` file is a gzipped tarball:

```
package-1.0.0.neampkg (tar.gz)
├── neam.toml           # Package manifest
├── README.md           # Documentation
├── LICENSE             # License file
├── src/                # Source code
│   └── lib.neam
├── checksums.sha256    # File checksums
└── signature.sig       # Optional: GPG signature
```

### Upload Handler (Frontend)

```typescript
// src/lib/publish.ts
import { supabase } from './supabase'
import * as tar from 'tar'
import pako from 'pako'

export async function publishPackage(file: File): Promise<PublishResult> {
  // 1. Read and extract package
  const arrayBuffer = await file.arrayBuffer()
  const decompressed = pako.ungzip(new Uint8Array(arrayBuffer))

  // 2. Parse tar to get neam.toml
  const manifest = await extractManifest(decompressed)

  // 3. Validate manifest
  validateManifest(manifest)

  // 4. Get auth token
  const { data: { session } } = await supabase.auth.getSession()
  if (!session) throw new Error('Not authenticated')

  // 5. Upload via edge function
  const formData = new FormData()
  formData.append('package', file)
  formData.append('manifest', JSON.stringify(manifest))

  const response = await fetch(
    `${import.meta.env.VITE_SUPABASE_URL}/functions/v1/packages-publish`,
    {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${session.access_token}`
      },
      body: formData
    }
  )

  const result = await response.json()
  if (!response.ok) throw new Error(result.error)

  return result
}

function validateManifest(manifest: any) {
  if (!manifest.package?.name) throw new Error('Missing package name')
  if (!manifest.package?.version) throw new Error('Missing package version')
  if (!/^\d+\.\d+\.\d+/.test(manifest.package.version)) {
    throw new Error('Invalid version format (use semver)')
  }
  if (!/^[a-z][a-z0-9_-]*$/.test(manifest.package.name)) {
    throw new Error('Invalid package name')
  }
}
```

---

## 8. Search Implementation

### Full-Text Search with PostgreSQL

The search uses PostgreSQL's built-in full-text search with weighted vectors:

- **Weight A**: Package name (highest priority)
- **Weight B**: Description
- **Weight C**: Keywords

### Search Component

```typescript
// src/components/PackageSearch.tsx
import { useState, useEffect, useCallback } from 'react'
import { useDebounce } from '@/hooks/useDebounce'
import { supabase } from '@/lib/supabase'
import { Input } from '@/components/ui/input'
import { SearchIcon } from 'lucide-react'

export function PackageSearch() {
  const [query, setQuery] = useState('')
  const [results, setResults] = useState<Package[]>([])
  const [loading, setLoading] = useState(false)

  const debouncedQuery = useDebounce(query, 300)

  const search = useCallback(async (q: string) => {
    if (!q.trim()) {
      setResults([])
      return
    }

    setLoading(true)
    try {
      const { data, error } = await supabase
        .rpc('search_packages', {
          search_query: q,
          limit_count: 20,
          offset_count: 0
        })

      if (error) throw error
      setResults(data || [])
    } catch (err) {
      console.error('Search error:', err)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    search(debouncedQuery)
  }, [debouncedQuery, search])

  return (
    <div className="relative">
      <SearchIcon className="absolute left-3 top-3 h-4 w-4 text-muted-foreground" />
      <Input
        placeholder="Search packages..."
        value={query}
        onChange={(e) => setQuery(e.target.value)}
        className="pl-10"
      />

      {loading && <div className="absolute right-3 top-3">Loading...</div>}

      {results.length > 0 && (
        <div className="absolute top-full mt-2 w-full bg-card border rounded-lg shadow-lg z-50">
          {results.map((pkg) => (
            <a
              key={pkg.id}
              href={`/packages/${pkg.name}`}
              className="block p-3 hover:bg-muted"
            >
              <div className="font-medium">{pkg.name}</div>
              <div className="text-sm text-muted-foreground truncate">
                {pkg.description}
              </div>
            </a>
          ))}
        </div>
      )}
    </div>
  )
}
```

---

## 9. Deployment Strategy

### Option A: Lovable + Supabase (Recommended for MVP)

```
┌─────────────────┐      ┌─────────────────┐
│  Lovable Host   │      │    Supabase     │
│  (Frontend)     │─────▶│  (Backend)      │
│                 │      │                 │
│  - React App    │      │  - Auth         │
│  - Vercel CDN   │      │  - PostgreSQL   │
│                 │      │  - Storage      │
│                 │      │  - Edge Funcs   │
└─────────────────┘      └─────────────────┘
         │
         ▼
┌─────────────────┐
│   Cloudflare    │
│   (CDN/DNS)     │
│                 │
│  - SSL          │
│  - Caching      │
│  - DDoS protect │
└─────────────────┘
```

### Option B: Self-Hosted (For Scale)

```
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│   Cloudflare    │      │    Fly.io/      │      │    Supabase/    │
│   (CDN/DNS)     │─────▶│    Railway      │─────▶│    Neon DB      │
└─────────────────┘      │   (Backend)     │      └─────────────────┘
                         └─────────────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │   R2/S3         │
                         │   (Storage)     │
                         └─────────────────┘
```

### DNS Configuration

```
registry.neam.dev     A     -> Cloudflare/Vercel IP
api.registry.neam.dev CNAME -> supabase project URL
```

### Environment Variables

```env
# Frontend (.env)
VITE_SUPABASE_URL=https://xxx.supabase.co
VITE_SUPABASE_ANON_KEY=eyJ...

# Supabase Edge Functions
SUPABASE_URL=https://xxx.supabase.co
SUPABASE_SERVICE_ROLE_KEY=eyJ...
```

---

## 10. Integration with neam-pkg CLI

### Update Registry Client

Update `NeamC/src/pkg/registry.cpp` to use the real API:

```cpp
// Default registry URL
RegistryClient::RegistryClient(const std::string& registry_url)
    : registry_url_(registry_url.empty() ? "https://registry.neam.dev" : registry_url)
{
    // ... existing code
}

// API endpoints map to Supabase edge functions
std::string RegistryClient::http_get(const std::string& endpoint)
{
    // /api/v1/packages/search -> /functions/v1/packages-search
    // /api/v1/packages/{name} -> /rest/v1/packages?name=eq.{name}
    // /api/v1/download/{name}/{version} -> /functions/v1/packages-download

    return make_request("GET", endpoint, "");
}
```

### Token Authentication

```cpp
bool RegistryClient::authenticate_with_token(const std::string& token)
{
    // Validate token with Supabase
    std::string response = http_get("/auth/v1/user");

    if (response.find("\"id\"") != std::string::npos)
    {
        auth_token_ = token;
        username_ = json::get_string(response, "email");
        return true;
    }

    return false;
}
```

### CLI Login Flow

```
$ neam-pkg login

→ Opening browser for authentication...
  https://registry.neam.dev/cli-auth?session=abc123

→ Waiting for authentication...
✓ Logged in as developer@example.com

Token saved to ~/.neam/credentials.toml
```

---

## Implementation Timeline

### Phase 1: MVP (Week 1-2)
- [ ] Set up Lovable project
- [ ] Create Supabase database schema
- [ ] Build homepage and search
- [ ] Implement basic auth
- [ ] Package detail page

### Phase 2: Publishing (Week 3)
- [ ] Upload edge function
- [ ] Download tracking
- [ ] User dashboard
- [ ] API token management

### Phase 3: Polish (Week 4)
- [ ] README rendering
- [ ] Download charts
- [ ] Categories/filtering
- [ ] CLI integration testing

### Phase 4: Launch (Week 5)
- [ ] Custom domain setup
- [ ] CDN configuration
- [ ] Documentation
- [ ] Seed with example packages

---

## Security Checklist

- [ ] Rate limiting on all endpoints
- [ ] Package name validation (no typosquatting)
- [ ] File size limits (max 50MB)
- [ ] Content-Type validation
- [ ] SQL injection prevention (parameterized queries)
- [ ] XSS prevention (sanitized markdown)
- [ ] CSRF protection
- [ ] Audit logging
- [ ] Two-factor authentication for publishers

---

## Cost Estimate

### Supabase (Free Tier → Pro)

| Resource | Free | Pro ($25/mo) |
|----------|------|--------------|
| Database | 500MB | 8GB |
| Storage | 1GB | 100GB |
| Bandwidth | 2GB | 250GB |
| Edge Functions | 500K/mo | 2M/mo |

### Cloudflare (Free Tier)

- SSL: Free
- CDN: Free
- DDoS Protection: Free

### Estimated Monthly Cost

| Stage | Users | Cost |
|-------|-------|------|
| MVP | <100 | $0 (free tiers) |
| Growth | 100-1000 | $25-50/mo |
| Scale | 1000+ | $100-500/mo |

---

## Next Steps

1. **Create Lovable Account**: https://lovable.dev
2. **Set Up Supabase Project**: https://supabase.com
3. **Use Initial Prompt**: Copy the homepage prompt into Lovable
4. **Run Database Migrations**: Execute SQL schema in Supabase
5. **Deploy and Test**: Publish to Lovable hosting
6. **Connect Domain**: Point registry.neam.dev to deployment

Would you like me to create any specific component in more detail?
