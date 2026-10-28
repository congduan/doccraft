// Copyright 2026 The doccraft Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Dumps the schema metadata that OpenXmlValidator uses at run time into a
// compact JSON file. The metadata is produced by reflection over
// DocumentFormat.OpenXml (MIT licensed); the JSON is a derived data file and
// the generated C++ tables are derived from it. Nothing here is linked into
// the doccraft binary - this tool is a development-time, offline step.
//
// Usage: dotnet run --project tools/schema-gen/DumpSdkMetadata -- <out.json>

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using DocumentFormat.OpenXml;
using DocumentFormat.OpenXml.Wordprocessing;

namespace Doccraft.SchemaGen;

internal static class Program
{
    private const BindingFlags Any =
        BindingFlags.Instance | BindingFlags.Static |
        BindingFlags.NonPublic | BindingFlags.Public;

    private static readonly PropertyInfo MetadataProperty =
        typeof(OpenXmlElement).GetProperty("Metadata", Any)!;

    private static readonly Dictionary<Type, object> MetadataCache = new();
    private static readonly Dictionary<Type, string> QNameCache = new();
    private static readonly List<string> Failures = new();

    private static int Main(string[] args)
    {
        var output = args.Length > 0 ? args[0] : "wml-metadata.json";

        var wmlAssembly = typeof(Paragraph).Assembly;
        var seeds = wmlAssembly.GetTypes()
            .Where(t => t.Namespace == "DocumentFormat.OpenXml.Wordprocessing")
            .Where(t => typeof(OpenXmlElement).IsAssignableFrom(t))
            .Where(t => !t.IsAbstract && !t.ContainsGenericParameters)
            .OrderBy(t => t.FullName, StringComparer.Ordinal)
            .ToList();

        var elements = new List<Dictionary<string, object?>>();
        var seen = new HashSet<Type>();
        var queue = new Queue<Type>(seeds);

        while (queue.Count > 0)
        {
            var type = queue.Dequeue();
            if (!seen.Add(type)) continue;

            var meta = GetMetadata(type);
            if (meta is null) continue;

            var qname = GetQName(type);
            if (qname is null) continue;

            var particle = BuildParticle(meta, queue);

            elements.Add(new Dictionary<string, object?>
            {
                ["clr"] = type.FullName,
                ["ns"] = qname.Value.Namespace,
                ["local"] = qname.Value.Local,
                ["version"] = Prop(meta, "Availability")?.ToString(),
                ["particle"] = particle,
                ["attributes"] = BuildAttributes(meta),
            });
        }

        elements.Sort((a, b) =>
        {
            var byLocal = string.CompareOrdinal(
                (string)a["local"]!, (string)b["local"]!);
            return byLocal != 0
                ? byLocal
                : string.CompareOrdinal((string)a["ns"]!, (string)b["ns"]!);
        });

        var document = new Dictionary<string, object?>
        {
            ["source"] = "DocumentFormat.OpenXml 3.3.0 (OpenXmlElement.Metadata)",
            ["elements"] = elements,
        };

        var options = new JsonSerializerOptions { WriteIndented = false };
        using (var stream = File.Create(output))
        {
            JsonSerializer.Serialize(stream, document, options);
        }

        Console.WriteLine(
            $"elements={elements.Count} failures={Failures.Count} -> {output}");
        foreach (var failure in Failures.Take(20))
        {
            Console.Error.WriteLine("skip: " + failure);
        }

        return Failures.Count == 0 ? 0 : 0;
    }

    private static object? Prop(object target, string name) =>
        target.GetType().GetProperty(name, Any)?.GetValue(target);

    private static object? GetMetadata(Type type)
    {
        if (MetadataCache.TryGetValue(type, out var cached)) return cached;

        object? metadata = null;
        try
        {
            var instance = (OpenXmlElement?)Activator.CreateInstance(type)
                ?? (OpenXmlElement)RuntimeHelpers.GetUninitializedObject(type);
            metadata = MetadataProperty.GetValue(instance);
        }
        catch (Exception e)
        {
            Failures.Add($"{type.FullName}: {e.GetType().Name}");
        }

        if (metadata is not null) MetadataCache[type] = metadata;
        return metadata;
    }

    private static (string Namespace, string Local)? GetQName(Type type)
    {
        if (QNameCache.TryGetValue(type, out var cached))
        {
            var parts = Split(cached);
            return parts;
        }

        var metadata = GetMetadata(type);
        if (metadata is null) return null;

        var text = Prop(metadata, "QName")?.ToString();
        if (string.IsNullOrEmpty(text)) return null;

        QNameCache[type] = text;
        return Split(text);
    }

    // OpenXmlQualifiedName.ToString() renders "<namespace>:<local>", and the
    // namespace itself contains "://", so split on the last colon.
    private static (string Namespace, string Local) Split(string qname)
    {
        var index = qname.LastIndexOf(':');
        return index < 0
            ? (string.Empty, qname)
            : (qname[..index], qname[(index + 1)..]);
    }

    // Leaf elements carry no particle at all; an empty sequence is the exact
    // equivalent (zero children allowed) and keeps every element in the table.
    private static List<Dictionary<string, object?>> BuildParticle(
        object metadata, Queue<Type> queue)
    {
        var result = new List<Dictionary<string, object?>>();
        var compiled = Prop(metadata, "Particle");
        if (compiled is null) return result;

        var root = Prop(compiled, "Particle");
        if (root is null) return result;

        WalkParticle(root, result, queue);
        return result;
    }

    private static void WalkParticle(
        object particle, List<Dictionary<string, object?>> sink, Queue<Type> queue)
    {
        var kind = Prop(particle, "ParticleType")?.ToString() ?? "Unknown";
        var node = new Dictionary<string, object?>
        {
            ["k"] = kind,
            ["min"] = ToOccurs(Prop(particle, "MinOccurs")),
            ["max"] = ToOccurs(Prop(particle, "MaxOccurs")),
        };

        if (kind == "Element")
        {
            var elementType = Prop(particle, "ElementType") as Type;
            if (elementType is not null)
            {
                var qname = GetQName(elementType);
                if (qname is null)
                {
                    node["k"] = "Any";
                    node["ns"] = "##any";
                }
                else
                {
                    node["ns"] = qname.Value.Namespace;
                    node["local"] = qname.Value.Local;
                    queue.Enqueue(elementType);
                }
            }
        }
        else if (kind == "Any")
        {
            node["ns"] = Prop(particle, "NamespaceValue")?.ToString() ?? "##any";
        }
        else if (Prop(particle, "ChildrenParticles") is System.Collections.IEnumerable kids)
        {
            var children = new List<Dictionary<string, object?>>();
            foreach (var child in kids)
            {
                if (child is null) continue;
                WalkParticle(child, children, queue);
            }

            node["ch"] = children;
        }

        sink.Add(node);
    }

    private static int ToOccurs(object? value) =>
        value is int number ? number : 1;

    private static List<Dictionary<string, object?>> BuildAttributes(object metadata)
    {
        var result = new List<Dictionary<string, object?>>();
        if (Prop(metadata, "Attributes") is not System.Collections.IEnumerable attrs)
        {
            return result;
        }

        foreach (var attr in attrs)
        {
            if (attr is null) continue;
            var qname = Prop(attr, "QName")?.ToString() ?? string.Empty;
            var split = Split(qname);
            result.Add(new Dictionary<string, object?>
            {
                ["ns"] = split.Namespace,
                ["local"] = split.Local,
                ["type"] = (Prop(attr, "Type") as Type)?.Name,
            });
        }

        return result;
    }
}
