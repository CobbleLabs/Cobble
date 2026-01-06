import clsx from "clsx";

interface Column<T> {
    header: string;
    accessorKey?: keyof T;
    cell?: (item: T) => React.ReactNode;
    className?: string;
}

interface TableProps<T> {
    data: T[];
    columns: Column<T>[];
    onRowClick?: (item: T) => void;
    isLoading?: boolean;
}

export function Table<T>({ data, columns, onRowClick, isLoading = false }: TableProps<T>) {
    return (
        <div className="w-full overflow-hidden rounded-xl border border-[var(--border-color)] bg-[var(--bg-primary)]">
            <div className="overflow-x-auto">
                <table className="w-full text-left text-sm">
                    <thead className="bg-[var(--bg-tertiary)]">
                        <tr>
                            {columns.map((col, i) => (
                                <th
                                    key={i}
                                    className={clsx(
                                        "px-6 py-4 font-medium text-[var(--text-muted)] uppercase tracking-wider text-xs",
                                        col.className
                                    )}
                                >
                                    {col.header}
                                </th>
                            ))}
                        </tr>
                    </thead>
                    <tbody className="divide-y divide-[var(--border-color)]">
                        {isLoading ? (
                            // Loading Skeleton
                            [...Array(5)].map((_, i) => (
                                <tr key={i} className="animate-pulse">
                                    {columns.map((__, j) => (
                                        <td key={j} className="px-6 py-4">
                                            <div className="h-4 bg-[var(--bg-tertiary)] rounded w-3/4" />
                                        </td>
                                    ))}
                                </tr>
                            ))
                        ) : data.length > 0 ? (
                            data.map((item, i) => (
                                <tr
                                    key={i}
                                    onClick={() => onRowClick && onRowClick(item)}
                                    className={clsx(
                                        "transition-colors hover:bg-[var(--bg-tertiary)]/50",
                                        onRowClick && "cursor-pointer"
                                    )}
                                >
                                    {columns.map((col, j) => (
                                        <td key={j} className="px-6 py-4 whitespace-nowrap text-[var(--text-secondary)]">
                                            {col.cell
                                                ? col.cell(item)
                                                : (item[col.accessorKey as keyof T] as React.ReactNode)}
                                        </td>
                                    ))}
                                </tr>
                            ))
                        ) : (
                            <tr>
                                <td colSpan={columns.length} className="px-6 py-12 text-center text-[var(--text-muted)]">
                                    No data found.
                                </td>
                            </tr>
                        )}
                    </tbody>
                </table>
            </div>
        </div>
    );
}
